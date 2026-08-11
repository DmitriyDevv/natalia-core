add_library(natalia_algorithm STATIC)

target_sources(natalia_algorithm PRIVATE
        src/actions.c
        src/alarm_monitor.c
        src/algorithm.c
        src/state.c
        src/observe.c
        src/ni_packet.c
        src/tlm_staging.c
        src/mram_store.c
        src/event_queue.c
)

target_include_directories(natalia_algorithm PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/BSP/Board_API/include
)

target_link_libraries(natalia_algorithm PUBLIC
        natalia_core_api
)

target_sources(natalia_algorithm PRIVATE
        src/transport.c
)

if (NATALIA_USE_BOARD_STUBS)
    target_sources(natalia_algorithm PRIVATE
            src/board_api_stub.c
    )

    target_compile_definitions(natalia_algorithm PUBLIC
            NATALIA_USE_BOARD_STUBS=1
    )
else ()
    # Algorithm depends only on the Board_API communication contract
    # (board_comm_*). The concrete backend (UniCAN, or the deterministic
    # disabled-comm fallback) is selected inside natalia_board_api_impl, so the
    # Algorithm library must not reference NATALIA_ENABLE_UNICAN_DRIVER here.

    if (NATALIA_ENABLE_DETECTOR_PROTO_LOG)
        target_link_libraries(natalia_algorithm PRIVATE
                natalia_diagnostics
        )

        target_compile_definitions(natalia_algorithm PRIVATE
                NATALIA_ENABLE_DETECTOR_PROTO_LOG=1
        )
    endif ()
endif ()

target_compile_options(natalia_algorithm PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wdouble-promotion
        -Wmissing-prototypes
        -Wstrict-prototypes
)
