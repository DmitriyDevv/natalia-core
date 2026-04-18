cmake_minimum_required(VERSION 3.28)
project(natalia_core C)

set(CMAKE_C_STANDARD 11)

add_executable(natalia_core main.c
        include/state.h
        src/state.c
        include/actions.h
        src/actions.c)

target_include_directories(natalia_core PRIVATE include)
