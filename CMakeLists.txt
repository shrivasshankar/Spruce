cmake_minimum_required(VERSION 3.20)

project(Spruce CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)

add_library(lsm src/durability.cpp src/memtable.cpp src/wal.cpp src/bloom.cpp src/sstable.cpp src/engine.cpp src/manifest.cpp)
target_include_directories(lsm PUBLIC include)

add_executable(spruce src/main.cpp)
target_link_libraries(spruce PRIVATE lsm)

add_executable(spruce_bench src/benchmark.cpp)
target_link_libraries(spruce_bench PRIVATE lsm)