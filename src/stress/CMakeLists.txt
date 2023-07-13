cmake_minimum_required(VERSION 2.8)

project(stress)

if(NOT DEFINED ENV{CMAKE_EXPORT_COMPILE_COMMANDS})
	message("ENV(CMAKE_EXPORT_COMPILE_COMMANDS) not defined, setting to TRUE")
	set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")
endif()

set(SOURCES main.cpp stress.cpp time.cpp)
set(HEADERS stress.hpp time.hpp)

add_executable(stress ${SOURCES} ${HEADERS})
