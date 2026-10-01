add_library(natalia_core_api INTERFACE)

target_include_directories(natalia_core_api INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(natalia_crc16 STATIC
        src/crc16_software.c
)

target_link_libraries(natalia_crc16 PUBLIC
        natalia_core_api
)

if (NATALIA_BUILD_TESTS)
    target_sources(natalia_crc16 PRIVATE
            src/crc16_select_software.c
    )
else()
    target_sources(natalia_crc16 PRIVATE
            src/crc16_hardware.c
    )

    target_link_libraries(natalia_crc16 PRIVATE
            natalia_cmsis
    )
endif()

target_compile_options(natalia_crc16 PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wmissing-prototypes
        -Wstrict-prototypes
        -ffunction-sections
        -fdata-sections
)

if (NOT NATALIA_BUILD_TESTS)
    add_library(natalia_core STATIC
            src/clock.c
            src/timebase.c
            src/watchdog.c
    )

    target_include_directories(natalia_core PRIVATE
            ${CMAKE_SOURCE_DIR}/Device/include
    )

    if (NATALIA_ENABLE_WATCHDOG)
        target_compile_definitions(natalia_core PRIVATE
                NATALIA_ENABLE_WATCHDOG=1
        )
    endif()

    target_link_libraries(natalia_core PUBLIC
            natalia_core_api
            natalia_crc16
            natalia_cmsis
    )

    target_compile_options(natalia_core PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wconversion
            -Wmissing-prototypes
            -Wstrict-prototypes
            -ffunction-sections
            -fdata-sections
    )
endif()
