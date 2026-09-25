cmake_minimum_required(VERSION 3.20)
project(sentinel VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(sentinel src/engine.cpp)
target_include_directories(sentinel PUBLIC include)
target_compile_options(sentinel PRIVATE -Wall -Wextra -Wpedantic)

add_executable(sentinel_demo apps/demo.cpp)
target_link_libraries(sentinel_demo PRIVATE sentinel)

add_executable(sentinel_tests tests/engine_tests.cpp)
target_link_libraries(sentinel_tests PRIVATE sentinel)

add_executable(sentinel_benchmark bench/benchmark.cpp)
target_link_libraries(sentinel_benchmark PRIVATE sentinel)

enable_testing()
add_test(NAME sentinel_tests COMMAND sentinel_tests)

