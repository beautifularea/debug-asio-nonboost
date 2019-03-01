//
// detail/impl/scheduler.ipp
// ~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2018 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef ASIO_DETAIL_IMPL_SCHEDULER_IPP
#define ASIO_DETAIL_IMPL_SCHEDULER_IPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "asio/detail/config.hpp"

#include "asio/detail/concurrency_hint.hpp"
#include "asio/detail/event.hpp"
#include "asio/detail/limits.hpp"
#include "asio/detail/reactor.hpp"
#include "asio/detail/scheduler.hpp"
#include "asio/detail/scheduler_thread_info.hpp"

#include "asio/detail/push_options.hpp"
#include <iostream>

namespace asio {
namespace detail {

struct scheduler::task_cleanup
{
  ~task_cleanup()
  {
    if (this_thread_->private_outstanding_work > 0)
    {
      asio::detail::increment(scheduler_->outstanding_work_,  this_thread_->private_outstanding_work);
    }

    this_thread_->private_outstanding_work = 0;

    // Enqueue the completed operations and reinsert the task at the end of
    // the operation queue.
    lock_->lock();

    std::cout << "在task_cleanup的析构函数中，把私有线程的任务队列push到op_queue_中。" << std::endl;

    scheduler_->task_interrupted_ = true;
    scheduler_->op_queue_.push(this_thread_->private_op_queue);
    scheduler_->op_queue_.push(&scheduler_->task_operation_);
  }

  scheduler* scheduler_;
  mutex::scoped_lock* lock_;
  thread_info* this_thread_;
};

struct scheduler::work_cleanup
{
  ~work_cleanup()
  {
    if (this_thread_->private_outstanding_work > 1)
    {
      asio::detail::increment(
          scheduler_->outstanding_work_,
          this_thread_->private_outstanding_work - 1);
    }
    else if (this_thread_->private_outstanding_work < 1)
    {
      scheduler_->work_finished();
    }
    this_thread_->private_outstanding_work = 0;

#if defined(ASIO_HAS_THREADS)
    if (!this_thread_->private_op_queue.empty())
    {
      lock_->lock();
      scheduler_->op_queue_.push(this_thread_->private_op_queue);
    }
#endif // defined(ASIO_HAS_THREADS)
  }

  scheduler* scheduler_;
  mutex::scoped_lock* lock_;
  thread_info* this_thread_;
};

scheduler::scheduler(asio::execution_context& ctx, int concurrency_hint)
  : asio::detail::execution_context_service_base<scheduler>(ctx),
    one_thread_(concurrency_hint == 1 || !ASIO_CONCURRENCY_HINT_IS_LOCKING(SCHEDULER, concurrency_hint) || !ASIO_CONCURRENCY_HINT_IS_LOCKING(REACTOR_IO, concurrency_hint)),
    mutex_(ASIO_CONCURRENCY_HINT_IS_LOCKING(SCHEDULER, concurrency_hint)),
    task_(0),
    task_interrupted_(true),
    outstanding_work_(0),
    stopped_(false),
    shutdown_(false),
    concurrency_hint_(concurrency_hint)
{
    std::cout << "进入到scheduler的构造函数。" << std::endl;
    std::cout << "concurrency_hit : " << concurrency_hint_ << std::endl;


  ASIO_HANDLER_TRACKING_INIT;
}

void scheduler::shutdown()
{
  mutex::scoped_lock lock(mutex_);
  shutdown_ = true;
  lock.unlock();

  // Destroy handler objects.
  while (!op_queue_.empty())
  {
    operation* o = op_queue_.front();
    op_queue_.pop();
    if (o != &task_operation_)
      o->destroy();
  }

  // Reset to initial state.
  task_ = 0;
}

void scheduler::init_task()
{
    std::cout << "进入到 scheduler 的 init_task方法。" << std::endl;

    mutex::scoped_lock lock(mutex_);
    if (!shutdown_ && !task_)
    {
        task_ = &use_service<reactor>(this->context());

        std::cout << "添加一个 task 到　op_queue队列。" << std::endl;
        op_queue_.push(&task_operation_);
        wake_one_thread_and_unlock(lock);
    }
}

std::size_t scheduler::run(asio::error_code& ec)
{
    std::cout << "进入到　scheduler::run　方法。" << std::endl;

    ec = asio::error_code();
    if (outstanding_work_ == 0)
    {
        stop();
        return 0;
    }

    thread_info this_thread;
    this_thread.private_outstanding_work = 0;
    thread_call_stack::context ctx(this, this_thread);

    mutex::scoped_lock lock(mutex_);

    std::cout << "调用 do_run_one 方法。" << std::endl;
    std::size_t n = 0;
    for (; do_run_one(lock, this_thread, ec); lock.lock())
        if (n != (std::numeric_limits<std::size_t>::max)())
        {
            std::cout << "do_run_one　循环次数　：　" << n << std::endl;
            ++n;
        }
    return n;
}

std::size_t scheduler::run_one(asio::error_code& ec)
{
    std::cout << "进入到 scheduler 的　run_one 方法。" << std::endl;

    ec = asio::error_code();
    if (outstanding_work_ == 0)
    {
        stop();
        return 0;
    }

    thread_info this_thread;
    this_thread.private_outstanding_work = 0;
    thread_call_stack::context ctx(this, this_thread);

    mutex::scoped_lock lock(mutex_);

    std::cout << "调用　do_run_one 方法。" << std::endl;
    return do_run_one(lock, this_thread, ec);
}

std::size_t scheduler::wait_one(long usec, asio::error_code& ec)
{
    ec = asio::error_code();
    if (outstanding_work_ == 0)
    {
        stop();
        return 0;
    }

    thread_info this_thread;
    this_thread.private_outstanding_work = 0;
    thread_call_stack::context ctx(this, this_thread);

    mutex::scoped_lock lock(mutex_);

    return do_wait_one(lock, this_thread, usec, ec);
}

std::size_t scheduler::poll(asio::error_code& ec)
{
    ec = asio::error_code();
    if (outstanding_work_ == 0)
    {
        stop();
        return 0;
    }

    thread_info this_thread;
    this_thread.private_outstanding_work = 0;
    thread_call_stack::context ctx(this, this_thread);

    mutex::scoped_lock lock(mutex_);

#if defined(ASIO_HAS_THREADS)
    // We want to support nested calls to poll() and poll_one(), so any handlers
    // that are already on a thread-private queue need to be put on to the main
    // queue now.
    if (one_thread_)
        if (thread_info* outer_info = static_cast<thread_info*>(ctx.next_by_key()))
            op_queue_.push(outer_info->private_op_queue);
#endif // defined(ASIO_HAS_THREADS)

    std::size_t n = 0;
    for (; do_poll_one(lock, this_thread, ec); lock.lock())
        if (n != (std::numeric_limits<std::size_t>::max)())
            ++n;
    return n;
}

std::size_t scheduler::poll_one(asio::error_code& ec)
{
    ec = asio::error_code();
    if (outstanding_work_ == 0)
    {
        stop();
        return 0;
    }

    thread_info this_thread;
    this_thread.private_outstanding_work = 0;
    thread_call_stack::context ctx(this, this_thread);

    mutex::scoped_lock lock(mutex_);

#if defined(ASIO_HAS_THREADS)
    // We want to support nested calls to poll() and poll_one(), so any handlers
    // that are already on a thread-private queue need to be put on to the main
    // queue now.
    if (one_thread_)
        if (thread_info* outer_info = static_cast<thread_info*>(ctx.next_by_key()))
            op_queue_.push(outer_info->private_op_queue);
#endif // defined(ASIO_HAS_THREADS)

    return do_poll_one(lock, this_thread, ec);
}

void scheduler::stop()
{
    mutex::scoped_lock lock(mutex_);
    stop_all_threads(lock);
}

bool scheduler::stopped() const
{
    mutex::scoped_lock lock(mutex_);
    return stopped_;
}

void scheduler::restart()
{
    mutex::scoped_lock lock(mutex_);
    stopped_ = false;
}

void scheduler::compensating_work_started()
{
    thread_info_base* this_thread = thread_call_stack::contains(this);
    ++static_cast<thread_info*>(this_thread)->private_outstanding_work;
}

void scheduler::post_immediate_completion(scheduler::operation* op, bool is_continuation)
{
    std::cout << "进入到 scheduler 的　post_immediate_completion方法。" << std::endl;

#if defined(ASIO_HAS_THREADS)
    if (one_thread_ || is_continuation)
    {
        if (thread_info_base* this_thread = thread_call_stack::contains(this))
        {
            ++static_cast<thread_info*>(this_thread)->private_outstanding_work;
            static_cast<thread_info*>(this_thread)->private_op_queue.push(op);
            return;
        }
    }
#else // defined(ASIO_HAS_THREADS)
    (void)is_continuation;
#endif // defined(ASIO_HAS_THREADS)

    work_started();

    mutex::scoped_lock lock(mutex_);
    std::cout << "加入一个tast." << std::endl;

    op_queue_.push(op);

    wake_one_thread_and_unlock(lock);
}

void scheduler::post_deferred_completion(scheduler::operation* op)
{
    std::cout << "进入到 post_deffered_completion　方法." << std::endl;

#if defined(ASIO_HAS_THREADS)
    if (one_thread_)
    {
        if (thread_info_base* this_thread = thread_call_stack::contains(this))
        {
            static_cast<thread_info*>(this_thread)->private_op_queue.push(op);
            return;
        }
    }
#endif // defined(ASIO_HAS_THREADS)

    mutex::scoped_lock lock(mutex_);
    op_queue_.push(op);
    wake_one_thread_and_unlock(lock);
}

void scheduler::post_deferred_completions(
        op_queue<scheduler::operation>& ops)
{
    if (!ops.empty())
    {
#if defined(ASIO_HAS_THREADS)
        if (one_thread_)
        {
            if (thread_info_base* this_thread = thread_call_stack::contains(this))
            {
                static_cast<thread_info*>(this_thread)->private_op_queue.push(ops);
                return;
            }
        }
#endif // defined(ASIO_HAS_THREADS)

        mutex::scoped_lock lock(mutex_);
        op_queue_.push(ops);
        wake_one_thread_and_unlock(lock);
    }
}

void scheduler::do_dispatch(scheduler::operation* op)
{
    std::cout << "进入到 do_dispath 方法。" << std::endl;

    work_started();

    mutex::scoped_lock lock(mutex_);
    op_queue_.push(op);

    wake_one_thread_and_unlock(lock);
}

void scheduler::abandon_operations(
        op_queue<scheduler::operation>& ops)
{
    op_queue<scheduler::operation> ops2;
    ops2.push(ops);
}

std::size_t scheduler::do_run_one(mutex::scoped_lock& lock, scheduler::thread_info& this_thread, const asio::error_code& ec)
{
    std::cout << "\n--------------------------------------进入到 scheduler的　do_run_one　方法。-----------------------------------------" << std::endl;
    while (!stopped_)
    {
        std::cout << "\n...进入事件循环..." << std::endl;
        if (!op_queue_.empty())
        {
            std::cout << "op_queue_ 队列不为空。" << std::endl;

            // Prepare to execute first handler from queue.
            operation* o = op_queue_.front();
            op_queue_.pop();
            bool more_handlers = (!op_queue_.empty());

            //std::cout << "还有　" << more_handlers << "　任务。" << std::endl;

            if (o == &task_operation_)
            {
                std::cout << "类型是 task_operation " << std::endl;

                task_interrupted_ = more_handlers;

                //唤醒另外一个线程来执行其它任务，本线程接着处理epoll_reactor
                if (more_handlers && !one_thread_)
                    wakeup_event_.unlock_and_signal_one(lock);
                else
                    lock.unlock();

                //干什么工作？
                task_cleanup on_exit = { this, &lock, &this_thread };
                (void)on_exit;

                // Run the task. May throw an exception. Only block if the operation
                // queue is empty and we're not polling, otherwise we want to return
                // as soon as possible.
                std::cout << "调用　task的run 方法。" << std::endl;
                task_->run(more_handlers ? 0 : -1, this_thread.private_op_queue);
            }
            else
            {
                std::size_t task_result = o->task_result_;

                if (more_handlers && !one_thread_)
                    wake_one_thread_and_unlock(lock);
                else
                    lock.unlock();

                // Ensure the count of outstanding work is decremented on block exit.
                work_cleanup on_exit = { this, &lock, &this_thread };
                (void)on_exit;

                std::cout << "不是　task_operation 类型，调用 opeartion->complete" << std::endl;
                // Complete the operation. May throw an exception. Deletes the object.
                o->complete(this, ec, task_result);

                return 1;
            }
        }
        else
        {
            std::cout << "任务队列为空，阻塞等待。" << std::endl;
            wakeup_event_.clear(lock);
            wakeup_event_.wait(lock);
        }
    }

    std::cout << "执行完毕 scheduler的　do_run_one　方法。\n" << std::endl;

    return 0;
}

std::size_t scheduler::do_wait_one(mutex::scoped_lock& lock,
        scheduler::thread_info& this_thread, long usec,
        const asio::error_code& ec)
{
    std::cout << "进入到 scheduler 的 do_wait_one 方法。" << std::endl;
    std::cout << "stopped_ : " << stopped_ << std::endl;

    if (stopped_)
        return 0;

    operation* o = op_queue_.front();
    if (o == 0)
    {
        wakeup_event_.clear(lock);
        wakeup_event_.wait_for_usec(lock, usec);
        usec = 0; // Wait at most once.
        o = op_queue_.front();
    }

    if (o == &task_operation_)
    {
        op_queue_.pop();
        bool more_handlers = (!op_queue_.empty());

        task_interrupted_ = more_handlers;

        if (more_handlers && !one_thread_)
            wakeup_event_.unlock_and_signal_one(lock);
        else
            lock.unlock();

        {
            task_cleanup on_exit = { this, &lock, &this_thread };
            (void)on_exit;

            // Run the task. May throw an exception. Only block if the operation
            // queue is empty and we're not polling, otherwise we want to return
            // as soon as possible.
            task_->run(more_handlers ? 0 : usec, this_thread.private_op_queue);
        }

        o = op_queue_.front();
        if (o == &task_operation_)
        {
            if (!one_thread_)
                wakeup_event_.maybe_unlock_and_signal_one(lock);
            return 0;
        }
    }

    if (o == 0)
        return 0;

    op_queue_.pop();
    bool more_handlers = (!op_queue_.empty());

    std::size_t task_result = o->task_result_;

    if (more_handlers && !one_thread_)
        wake_one_thread_and_unlock(lock);
    else
        lock.unlock();

    // Ensure the count of outstanding work is decremented on block exit.
    work_cleanup on_exit = { this, &lock, &this_thread };
    (void)on_exit;

    // Complete the operation. May throw an exception. Deletes the object.
    o->complete(this, ec, task_result);

    return 1;
}

std::size_t scheduler::do_poll_one(mutex::scoped_lock& lock,
        scheduler::thread_info& this_thread,
        const asio::error_code& ec)
{
    if (stopped_)
        return 0;

    operation* o = op_queue_.front();
    if (o == &task_operation_)
    {
        op_queue_.pop();
        lock.unlock();

        {
            task_cleanup c = { this, &lock, &this_thread };
            (void)c;

            // Run the task. May throw an exception. Only block if the operation
            // queue is empty and we're not polling, otherwise we want to return
            // as soon as possible.
            task_->run(0, this_thread.private_op_queue);
        }

        o = op_queue_.front();
        if (o == &task_operation_)
        {
            wakeup_event_.maybe_unlock_and_signal_one(lock);
            return 0;
        }
    }

    if (o == 0)
        return 0;

    op_queue_.pop();
    bool more_handlers = (!op_queue_.empty());

    std::size_t task_result = o->task_result_;

    if (more_handlers && !one_thread_)
        wake_one_thread_and_unlock(lock);
    else
        lock.unlock();

    // Ensure the count of outstanding work is decremented on block exit.
    work_cleanup on_exit = { this, &lock, &this_thread };
    (void)on_exit;

    // Complete the operation. May throw an exception. Deletes the object.
    o->complete(this, ec, task_result);

    return 1;
}

void scheduler::stop_all_threads(
        mutex::scoped_lock& lock)
{
    stopped_ = true;
    wakeup_event_.signal_all(lock);

    if (!task_interrupted_ && task_)
    {
        task_interrupted_ = true;
        task_->interrupt();
    }
}

void scheduler::wake_one_thread_and_unlock(
        mutex::scoped_lock& lock)
{
    if (!wakeup_event_.maybe_unlock_and_signal_one(lock))
    {
        if (!task_interrupted_ && task_)
        {
            task_interrupted_ = true;
            task_->interrupt();
        }
        lock.unlock();
    }
}

} // namespace detail
} // namespace asio

#include "asio/detail/pop_options.hpp"

#endif // ASIO_DETAIL_IMPL_SCHEDULER_IPP
