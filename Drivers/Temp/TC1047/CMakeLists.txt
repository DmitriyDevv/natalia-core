add_library(natalia_tc1047 STATIC
        src/tc1047.c
)

target_include_directories(natalia_tc1047 PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(natalia_tc1047 PUBLIC
        natalia_core_api
        natalia_bsp
)

target_compile_options(natalia_tc1047 PRIVATE
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