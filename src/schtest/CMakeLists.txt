cmake_minimum_required(VERSION 2.8)

project(schtest)

if(NOT DEFINED ENV{CMAKE_EXPORT_COMPILE_COMMANDS})
	message("ENV(CMAKE_EXPORT_COMPILE_COMMANDS) not defined, setting to TRUE")
	set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")
endif()

set(SOURCES main.c schtest.c smt.c cookie.c time.c cpuset.c)
set(HEADERS schtest.h smt.h cookie.h time.h cpuset.h)

add_executable(schtest ${SOURCES} ${HEADERS})
