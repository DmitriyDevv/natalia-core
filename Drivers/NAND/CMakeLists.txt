add_library(natalia_nand STATIC
        src/nand_mt29f.c
        src/nand_mt29f_commands.c
        src/nand_bbm.c
        src/nand_storage.c
        src/nand_test.c
)

target_include_directories(natalia_nand PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(natalia_nand PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(natalia_nand PUBLIC
        natalia_core_api
        natalia_bsp
)

target_compile_definitions(natalia_nand PUBLIC
        NAND_MT29F_CACHE_READ_MODE=${NATALIA_NAND_CACHE_READ_MODE}
        NAND_MT29F_PROGRAM_LOAD_MODE=${NATALIA_NAND_PROGRAM_LOAD_MODE}
)

target_compile_options(natalia_nand PRIVATE
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