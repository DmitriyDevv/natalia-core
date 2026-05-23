add_library(natalia_device STATIC
        src/board_pins.c
        src/system_stm32l4xx.c
        startup_stm32l496zgtx.s
)

target_include_directories(natalia_device PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(natalia_device PUBLIC
        natalia_core_api
        natalia_cmsis
)

target_compile_options(natalia_device PRIVATE
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