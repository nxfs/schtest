cmake_minimum_required(VERSION 3.16)

project(load)

if(NOT DEFINED ENV{CMAKE_EXPORT_COMPILE_COMMANDS})
	message("ENV(CMAKE_EXPORT_COMPILE_COMMANDS) not defined, setting to TRUE")
	set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")
endif()

set(SOURCES main.cpp load.cpp time.cpp)
set(HEADERS load.hpp time.hpp)

add_executable(load ${SOURCES} ${HEADERS})
