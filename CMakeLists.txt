cmake_minimum_required(VERSION 3.20)
project(sentinel VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(sentinel src/engine.cpp src/itch.cpp src/audit.cpp src/csv.cpp)
target_include_directories(sentinel PUBLIC include)
target_compile_options(sentinel PRIVATE -Wall -Wextra -Wpedantic -Werror)

find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)

add_executable(demo apps/demo.cpp)
target_link_libraries(demo PRIVATE sentinel)

add_executable(benchmark bench/benchmark.cpp)
target_link_libraries(benchmark PRIVATE sentinel)

add_executable(itch_replay apps/itch_replay.cpp)
target_link_libraries(itch_replay PRIVATE sentinel Threads::Threads ZLIB::ZLIB)

add_executable(csv_ingest apps/csv_ingest.cpp)
target_link_libraries(csv_ingest PRIVATE sentinel)

file(GLOB TEST_SOURCES tests/*.cpp)
add_executable(tests ${TEST_SOURCES})
target_link_libraries(tests PRIVATE sentinel Threads::Threads)

enable_testing()
add_test(NAME tests COMMAND tests)
