add_library(natalia_mram STATIC
        src/mram.c
)

target_include_directories(natalia_mram PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(natalia_mram PUBLIC
        natalia_core_api
        natalia_bsp
)

target_compile_options(natalia_mram PRIVATE
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
