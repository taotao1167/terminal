cmake_minimum_required(VERSION 2.9)
project(test)

SET(EXE_SRCLIST example.c terminal.c tt_buffer.c)
add_executable(example ${EXE_SRCLIST})
