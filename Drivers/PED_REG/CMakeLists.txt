add_library(natalia_ped_reg STATIC
        src/ped_reg.c
        src/ped_reg_ring.c
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

if (NATALIA_ENABLE_PED_INJECT)
    target_compile_definitions(natalia_ped_reg PUBLIC
            NATALIA_PED_REG_INJECT=1
    )
endif()

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