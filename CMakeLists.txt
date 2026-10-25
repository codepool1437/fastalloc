cmake_minimum_required(VERSION 3.14)
project(fastalloc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")

find_package(Threads REQUIRED)

add_library(fastalloc src/fastalloc.cpp)
target_include_directories(fastalloc PUBLIC include)
target_link_libraries(fastalloc PUBLIC Threads::Threads)

add_executable(test_fastalloc tests/test_fastalloc.cpp)
target_link_libraries(test_fastalloc fastalloc)

add_executable(bench bench/bench.cpp)
target_link_libraries(bench fastalloc)

enable_testing()
add_test(NAME fastalloc_tests COMMAND test_fastalloc)
