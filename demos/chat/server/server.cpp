//
// chat_server.cpp
// ~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2018 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <cstdlib>
#include <deque>
#include <iostream>
#include <list>
#include <memory>
#include <set>
#include <utility>
#include "asio.hpp"
#include "../message.hpp"
#include <thread>

using asio::ip::tcp;

//----------------------------------------------------------------------

typedef std::deque<chat_message> chat_message_queue;

//----------------------------------------------------------------------

class chat_participant
{
public:
  virtual ~chat_participant() {}
  virtual void deliver(const chat_message& msg) = 0;
};

typedef std::shared_ptr<chat_participant> chat_participant_ptr;

//----------------------------------------------------------------------

class chat_room
{
public:
  void join(chat_participant_ptr participant)
  {
    participants_.insert(participant);
    for (auto msg: recent_msgs_)
      participant->deliver(msg);
  }

  void leave(chat_participant_ptr participant)
  {
    participants_.erase(participant);
  }

  void deliver(const chat_message& msg)
  {
    recent_msgs_.push_back(msg);
    while (recent_msgs_.size() > max_recent_msgs)
      recent_msgs_.pop_front();

    for (auto participant: participants_)
      participant->deliver(msg);
  }

private:
  std::set<chat_participant_ptr> participants_;
  enum { max_recent_msgs = 100 };
  chat_message_queue recent_msgs_;
};

//----------------------------------------------------------------------

class chat_session
  : public chat_participant,
    public std::enable_shared_from_this<chat_session>
{
public:
  chat_session(tcp::socket socket, chat_room& room)
    : socket_(std::move(socket)),
      room_(room)
  {
  }

  void start()
  {
    room_.join(shared_from_this());
    do_read_header();
  }

  void deliver(const chat_message& msg)
  {
    bool write_in_progress = !write_msgs_.empty();
    write_msgs_.push_back(msg);
    if (!write_in_progress)
    {
      do_write();
    }
  }

private:
  void do_read_header()
  {
    auto self(shared_from_this());
    std::cout << "-------开始异步读取header----------" << std::endl;
    asio::async_read(socket_,
        asio::buffer(read_msg_.data(), chat_message::header_length),
        [this, self](std::error_code ec, std::size_t /*length*/)
        {
            std::cout << "header读取完毕。" << std::endl;

          if (!ec && read_msg_.decode_header())
          {
            do_read_body();
          }
          else
          {
            room_.leave(shared_from_this());
          }
        });
    std::cout << "-----结束异步读取header---------" << std::endl;
  }

  void do_read_body()
  {
    auto self(shared_from_this());

    std::cout << "-------开始异步读取body----------" << std::endl;
    asio::async_read(socket_,
        asio::buffer(read_msg_.body(), read_msg_.body_length()),
        [this, self](std::error_code ec, std::size_t /*length*/)
        {
            std::cout << "读取body完成。" << std::endl;

          if (!ec)
          {
            room_.deliver(read_msg_);
            do_read_header();
          }
          else
          {
            room_.leave(shared_from_this());
          }
        });

    std::cout << "-----结束异步读取body---------" << std::endl;
  }

  void do_write()
  {
    auto self(shared_from_this());

    std::cout << "-------------------开始异步写操作-------------------" << std::endl;
    asio::async_write(socket_,
        asio::buffer(write_msgs_.front().data(),
          write_msgs_.front().length()),
        [this, self](std::error_code ec, std::size_t /*length*/)
        {
            std::cout << "server 中写完成的回调。" << std::endl;
          if (!ec)
          {
            write_msgs_.pop_front();
            if (!write_msgs_.empty())
            {
              do_write();
            }
          }
          else
          {
            room_.leave(shared_from_this());
          }
        });

    std::cout << "--------------------结束异步写操作------------------" << std::endl;
  }

  tcp::socket socket_;
  chat_room& room_;
  chat_message read_msg_;
  chat_message_queue write_msgs_;
};

//----------------------------------------------------------------------

class chat_server
{
public:
  chat_server(asio::io_context& io_context, const tcp::endpoint& endpoint)
    : acceptor_(io_context, endpoint)
  {
        do_accept();
  }

private:
  void do_accept()
  {
    std::cout << "\n\n作为服务器，准备开始接受client的链接..." << std::endl;
    std::cout << "使用async_accept开启..." << std::endl;
    acceptor_.async_accept(
        [this](std::error_code ec, tcp::socket socket)
        {
            
          if (!ec)
          {
            std::cout << "接收到新链接过来，创建房间..." << std::endl;
            std::make_shared<chat_session>(std::move(socket), room_)->start();
          }

          do_accept();
        });
    std::cout << "作为异步接收，立即返回。\n\n" << std::endl;
  }

  tcp::acceptor acceptor_;
  chat_room room_;
};

//----------------------------------------------------------------------

void thread_2_f(void)
{
  {
    std::cout << "作为服务器，开始 io_context.run() 2 方法。" << std::endl;
    asio::io_context io_context;
    io_context.run();
  }
}

int main(int argc, char* argv[])
{

  try
  {
    if (argc < 2)
    {
      std::cerr << "Usage: chat_server <port> [<port> ...]\n";
      return 1;
    }

    asio::io_context io_context;

    std::list<chat_server> servers;
    //for (int i = 1; i < argc; ++i)
    {
      tcp::endpoint endpoint(tcp::v4(), std::atoi(argv[1]));
      servers.emplace_back(io_context, endpoint);
    }

    std::cout << "作为服务器，开始 io_context.run()方法。" << std::endl;
    io_context.run();
  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

//    std::thread thread_2(thread_2_f);
//    thread_2.join();

  return 0;
}
