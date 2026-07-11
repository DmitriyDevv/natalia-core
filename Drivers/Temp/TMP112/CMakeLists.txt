add_library(natalia_tmp112 STATIC
        src/tmp112.c
)

target_include_directories(natalia_tmp112 PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(natalia_tmp112 PUBLIC
        natalia_core_api
        natalia_bsp
)

target_compile_options(natalia_tmp112 PRIVATE
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