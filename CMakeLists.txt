cmake_minimum_required(VERSION 2.8)

project(schtest)

set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_subdirectory(src/stress)
add_subdirectory(src/multistress)
