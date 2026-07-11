add_library(natalia_ina219 STATIC
        src/ina219.c
        src/ina219_config.c
)

target_include_directories(natalia_ina219 PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(natalia_ina219 PUBLIC
        natalia_core_api
        natalia_bsp
)

target_compile_options(natalia_ina219 PRIVATE
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