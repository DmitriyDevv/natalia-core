add_library(natalia_diagnostics STATIC
        src/detector_log.c
)

target_include_directories(natalia_diagnostics PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(natalia_diagnostics PUBLIC
        natalia_core
        natalia_bsp
        natalia_unican
)

target_compile_options(natalia_diagnostics PRIVATE
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