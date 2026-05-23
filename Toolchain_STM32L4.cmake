set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR cortex-m4)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_OBJCOPY arm-none-eabi-objcopy)
set(CMAKE_SIZE arm-none-eabi-size)

set(CPU_FLAGS
        -mcpu=cortex-m4
        -mthumb
        -mfpu=fpv4-sp-d16
        -mfloat-abi=hard
)

set(COMMON_FLAGS
        ${CPU_FLAGS}
        -ffunction-sections
        -fdata-sections
        -fno-common
)

add_compile_options(
        ${COMMON_FLAGS}
)

add_link_options(
        ${CPU_FLAGS}
#        -nostartfiles
        --specs=nano.specs
#        --specs=nosys.specs
        -Wl,--gc-sections
)