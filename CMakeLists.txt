cmake_minimum_required(VERSION 2.9)
project(terminal)

set(CMAKE_C_FLAGS "-g -Wall")
add_executable(testapp testapp.c terminal.c tt_buffer.c)
target_link_libraries(testapp PUBLIC pthread)

if(CMAKE_HOST_SYSTEM_NAME MATCHES "Linux")
elseif(CMAKE_HOST_SYSTEM_NAME MATCHES "Windows")
else()
	message(FATAL_ERROR "unsupported platform: ${CMAKE_HOST_SYSTEM_NAME}")
endif()

