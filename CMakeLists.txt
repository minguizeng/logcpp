cmake_minimum_required(VERSION 3.8)
project("logcpp")

set(CMAKE_CXX_STANDARD 11)

add_compile_options(-ggdb -O2)

add_executable(main main.cc)
target_link_libraries(main -lpthread)
