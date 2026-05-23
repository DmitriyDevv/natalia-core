add_library(natalia_algorithm STATIC)

target_sources(natalia_algorithm PRIVATE
        src/actions.c
        src/state.c
        src/observe.c
        src/mram_store.c
)

target_include_directories(natalia_algorithm PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(natalia_algorithm PUBLIC
        natalia_core_api
)

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

if (NATALIA_BUILD_TESTS)
    target_sources(natalia_algorithm
            PRIVATE
            src/transport_stub.c
    )
else ()
    target_sources(natalia_algorithm
            PRIVATE
            src/transport.c
    )

    target_link_libraries(natalia_algorithm
            PUBLIC
            natalia_unican
    )
    if (NATALIA_ENABLE_DETECTOR_PROTO_LOG)
        target_link_libraries(natalia_algorithm PRIVATE
                natalia_diagnostics
        )

        target_compile_definitions(natalia_algorithm PRIVATE
                NATALIA_ENABLE_DETECTOR_PROTO_LOG=1
        )
    endif()
endif ()

if (NATALIA_USE_BOARD_STUBS)
    target_sources(natalia_algorithm PRIVATE
            src/board_api_stub.c
            src/transport_stub.c
    )

    target_compile_definitions(natalia_algorithm PUBLIC
            NATALIA_USE_BOARD_STUBS=1
    )
endif ()