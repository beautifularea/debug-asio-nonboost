/*
编译：g++ b.cc -o t -lboost_system -lboost_coroutine
*/
#include <iostream>
#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>

boost::asio::io_service io_service;

void other_work()
{
  std::cout << "Other work" << std::endl;
}

void other_work_1()
{
    std::cout << "other work 1" << std::endl;
}

void my_work(boost::asio::yield_context yield_context)
{

  // Wait on a timer within the coroutine.
  boost::asio::deadline_timer timer(io_service);
  timer.expires_from_now(boost::posix_time::seconds(1));
  std::cout << "Start wait" << std::endl;
  timer.async_wait(yield_context);
  std::cout << "Woke up" << std::endl;    
}

int main ()
{
  boost::asio::spawn(io_service, &my_work);

  // Add more work to the io_service.
  io_service.post(&other_work);
  io_service.post(&other_work_1);
  io_service.run();
}
/*
Here is an attempt to illustrate the execution of the example. Paths in | indicate the active stack, : indicates the suspended stack, and arrows are used to indicate transfer of control:

boost::asio::io_service io_service;
boost::asio::spawn(io_service, &my_work);
`-- dispatch a coroutine creator
    into the io_service.
io_service.run();
|-- invoke the coroutine creator
|   handler.
|   |-- create and jump into
|   |   into coroutine         ----> my_work()
:   :                                |-- post &other_work onto
:   :                                |   the io_service
:   :                                |-- create timer
:   :                                |-- set timer expiration
:   :                                |-- cout << "Start wait" << endl;
:   :                                |-- timer.async_wait(yield)
:   :                                |   |-- create error_code on stack
:   :                                |   |-- initiate async_wait operation,
:   :                                |   |   passing in completion handler that
:   :                                |   |   will resume the coroutine
|   `-- return                 <---- |   |-- yield
|-- io_service has work (the         :   :
|   &other_work and async_wait)      :   :
|-- invoke other_work()              :   :
|   `-- cout << "Other work"         :   :
|       << endl;                     :   :
|-- io_service still has work        :   :
|   (the async_wait operation)       :   :
|   ...async wait completes...       :   :
|-- invoke completion handler        :   :
|   |-- copies error_code            :   :
|   |   provided by service          :   :
|   |   into the one on the          :   :
|   |   coroutine stack              :   :
|   |-- resume                 ----> |   `-- return error code
:   :                                |-- cout << "Woke up." << endl;
:   :                                |-- exiting my_work block, timer is 
:   :                                |   destroyed.
|   `-- return                 <---- `-- coroutine done, yielding
`-- no outstanding work in 
    io_service, return.

*/
