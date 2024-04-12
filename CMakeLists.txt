cmake_minimum_required(VERSION 3.27)
project(High_Performance_Server)

set(CMAKE_CXX_STANDARD 17)

add_executable(High_Performance_Server main.cpp)

add_executable(ReadHttp
        C8/ReadHttp.cpp)