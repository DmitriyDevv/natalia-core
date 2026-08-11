add_library(natalia_core_api INTERFACE)

target_include_directories(natalia_core_api INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if (NOT NATALIA_BUILD_TESTS)
    add_library(natalia_core STATIC
            src/clock.c
            src/timebase.c
            src/watchdog.c
            src/crc16.c
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