#include <boost/asio/yield.hpp>
#include <boost/asio/coroutine.hpp>
#include <iostream>

boost::asio::coroutine c;

void foo(int i)
{
    reenter(c)
    {
        yield std::cout<<"foo1 "<<i<<std::endl;
        fork foo(100);
        yield std::cout<<"foo2 "<< i+1<<std::endl;
    }
}
int main()
{
    std::cout << "-----------enter main-------------" << std::endl;

    std::cout << "call foo(1) ..." << std::endl;
    foo(1);
    std::cout << "end foo(1) ..." << std::endl;

    std::cout << "call foo(2) ..." << std::endl;
    foo(2);
    std::cout << "end foo(2) ..." << std::endl;

    std::cout << "call foo(3) ..." << std::endl;
    foo(3);
    std::cout << "end foo(3) ..." << std::endl;

    std::cout << "-------------end main--------------" << std::endl;
    return 0;
}
