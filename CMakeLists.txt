cmake_minimum_required(VERSION 3.28)
project(natalia_core C)

set(CMAKE_C_STANDARD 11)

add_executable(natalia_core main.c
        include/state.h
        src/state.c
        include/actions.h
        src/actions.c
        include/mram_store.h
        src/mram_store.c
        include/board_api.h
        src/board_api_stub.c
        src/observe.c
        include/observe.h
        src/transport.c
        include/transport.h)

target_include_directories(natalia_core PRIVATE include)
