add_library(natalia_ped_reg STATIC
        src/ped_reg.c
)

target_include_directories(natalia_ped_reg PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(natalia_ped_reg PUBLIC
        natalia_core_api
        natalia_device
        natalia_bsp
        natalia_cmsis
)

target_compile_options(natalia_ped_reg PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wdouble-promotion
        -Wmissing-prototypes
        -Wstrict-prototypes
        -ffunction-sections
        -fdata-sections
)