add_library(natalia_unican STATIC
        src/unican.c
        src/unican_crc.c
        src/unican_port.c
)

target_include_directories(natalia_unican PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(natalia_unican PUBLIC
        natalia_core_api
        natalia_bsp
)

target_compile_options(natalia_unican PRIVATE
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