cmake_minimum_required(VERSION 3.20)

project(natalia_core_project C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# -----------------------------------------------------------------------------
# Build options
# -----------------------------------------------------------------------------

option(NATALIA_BUILD_TESTS
        "Build desktop tests instead of STM32 firmware"
        OFF)

option(NATALIA_USE_BOARD_STUBS
        "Use board_api stubs instead of real firmware implementation"
        ON)

option(NATALIA_ENABLE_DEBUG_LOG
        "Enable debug logging through development UART"
        OFF)

option(NATALIA_ENABLE_DETECTOR_PROTO_LOG
        "Enable DSTLOG UniCAN protocol log for detector test application"
        OFF)

option(NATALIA_ENABLE_UNICAN_DRIVER
        "Build UniCAN protocol driver"
        OFF)

option(NATALIA_ENABLE_PED_REG_DRIVER
        "Build PED_REG driver"
        OFF)

option(NATALIA_ENABLE_USB_DEVICE_DRIVER
        "Build USB Device driver"
        OFF)

set(NATALIA_NAND_PS_OFF_LEVEL "HIGH" CACHE STRING
        "NAND power-switch OFF level: HIGH or LOW. Must match installed load switch."
)

set_property(CACHE NATALIA_NAND_PS_OFF_LEVEL PROPERTY STRINGS HIGH LOW)

# -----------------------------------------------------------------------------
# Configuration checks
# -----------------------------------------------------------------------------

if (NATALIA_BUILD_TESTS AND NATALIA_ENABLE_DETECTOR_PROTO_LOG)
    message(FATAL_ERROR
            "NATALIA_ENABLE_DETECTOR_PROTO_LOG is available only for firmware builds"
    )
endif()

if (NATALIA_ENABLE_DETECTOR_PROTO_LOG AND NOT NATALIA_ENABLE_DEBUG_LOG)
    message(FATAL_ERROR
            "NATALIA_ENABLE_DETECTOR_PROTO_LOG requires NATALIA_ENABLE_DEBUG_LOG=ON"
    )
endif()

if (NATALIA_ENABLE_DETECTOR_PROTO_LOG AND NOT NATALIA_ENABLE_UNICAN_DRIVER)
    message(FATAL_ERROR
            "NATALIA_ENABLE_DETECTOR_PROTO_LOG requires NATALIA_ENABLE_UNICAN_DRIVER=ON"
    )
endif()

if (NOT NATALIA_NAND_PS_OFF_LEVEL STREQUAL "HIGH" AND
        NOT NATALIA_NAND_PS_OFF_LEVEL STREQUAL "LOW")
    message(FATAL_ERROR
            "Set -DNATALIA_NAND_PS_OFF_LEVEL=HIGH or LOW"
    )
endif()

if (NATALIA_BUILD_TESTS AND NOT NATALIA_USE_BOARD_STUBS)
    message(FATAL_ERROR
            "Desktop tests require -DNATALIA_USE_BOARD_STUBS=ON"
    )
endif()

# -----------------------------------------------------------------------------
# Desktop tests
# -----------------------------------------------------------------------------

if (NATALIA_BUILD_TESTS)
    add_subdirectory(Core)
    add_subdirectory(Algorithm)

    enable_testing()
    add_subdirectory(tests)

    return()
endif()

add_subdirectory(Drivers/CMSIS)
add_subdirectory(Core)
add_subdirectory(Device)
add_subdirectory(BSP)
add_subdirectory(Drivers)

if (NATALIA_ENABLE_DETECTOR_PROTO_LOG)
    add_subdirectory(Diagnostics)
endif()

add_subdirectory(Algorithm)

# -----------------------------------------------------------------------------
# Firmware executable
# -----------------------------------------------------------------------------

add_executable(natalia_firmware
        main.c
        Device/src/syscalls.c
        Device/src/sysmem.c
)

target_link_libraries(natalia_firmware PRIVATE
        natalia_algorithm
        natalia_core
        natalia_device
        natalia_bsp
        natalia_drivers
        natalia_cmsis
)

if (NOT NATALIA_USE_BOARD_STUBS)
    target_link_libraries(natalia_firmware PRIVATE
            natalia_board_api_impl
    )
endif()

if (NATALIA_ENABLE_DETECTOR_PROTO_LOG)
    target_link_libraries(natalia_firmware PRIVATE
            natalia_diagnostics
    )
endif()

target_compile_options(natalia_firmware PRIVATE
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

target_link_options(natalia_firmware PRIVATE
        -Wl,--gc-sections
        -Wl,-Map=${CMAKE_BINARY_DIR}/natalia_firmware.map
        -T${CMAKE_SOURCE_DIR}/Device/STM32L496ZGTX_FLASH.ld
)

# syscalls/sysmem are generated low-level newlib support files.
# They do not need to satisfy the strict public-function warning policy.
set_source_files_properties(
        Device/src/syscalls.c
        Device/src/sysmem.c
        PROPERTIES
        COMPILE_OPTIONS
        "-Wno-missing-prototypes;-Wno-strict-prototypes;-Wno-conversion"
)

add_custom_command(TARGET natalia_firmware POST_BUILD
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:natalia_firmware>

        COMMAND ${CMAKE_OBJCOPY} -O ihex
        $<TARGET_FILE:natalia_firmware>
        ${CMAKE_BINARY_DIR}/natalia_firmware.hex

        COMMAND ${CMAKE_OBJCOPY} -O binary
        $<TARGET_FILE:natalia_firmware>
        ${CMAKE_BINARY_DIR}/natalia_firmware.bin

        COMMENT "Generating firmware HEX and BIN files"
)