project(demo)
cmake_minimum_required(VERSION 2.8)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
set(CMAKE_CXX_FLAGSD "${CMAKE_CXX_FLAGS} -O0 -Wall -g -ggdb")

aux_source_directory(./ SRCS)
add_executable(demo ${SRCS})

include_directories(../include/)
target_link_libraries(demo -lpthread)


