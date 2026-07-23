cmake_minimum_required(VERSION 3.20)

project(natalia_core_project C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Default to an optimized build. Without this, single-config generators leave
# CMAKE_BUILD_TYPE empty, which means -O0 and dramatically slower firmware.
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING
            "Build type (Debug, Release, RelWithDebInfo, MinSizeRel)" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
            Debug Release RelWithDebInfo MinSizeRel)
endif()

# -----------------------------------------------------------------------------
# Build options
# -----------------------------------------------------------------------------

option(NATALIA_BUILD_TESTS
        "Build desktop tests instead of STM32 firmware"
        OFF)

option(NATALIA_USE_BOARD_STUBS
        "Use board_api stubs instead of real firmware implementation"
        ON)

set(NATALIA_LOG_BACKEND "LPUART1" CACHE STRING
        "Log output backend: NONE, LPUART1, USART2, FTDI or CAN"
)
set_property(CACHE NATALIA_LOG_BACKEND PROPERTY STRINGS NONE LPUART1 USART2 FTDI CAN)

option(NATALIA_ENABLE_DETECTOR_PROTO_LOG
        "Enable DSTLOG UniCAN protocol log for detector test application"
        OFF)

option(NATALIA_ENABLE_UNICAN_DRIVER
        "Build UniCAN protocol driver"
        OFF)

option(NATALIA_ENABLE_PED_REG_DRIVER
        "Build PED_REG driver"
        OFF)

option(NATALIA_ENABLE_I2C_DRIVER
        "Build BSP I2C master driver"
        OFF)

option(NATALIA_ENABLE_SPI_DRIVER
        "Build BSP SPI master driver"
        OFF)

option(NATALIA_ENABLE_MRAM_DRIVER
        "Build MR25H40 MRAM driver (SPI)"
        OFF)

option(NATALIA_ENABLE_INA219_DRIVER
        "Build INA219 power monitor driver"
        OFF)

option(NATALIA_ENABLE_ADC_DRIVER
        "Build BSP ADC DMA driver"
        OFF)

option(NATALIA_ADC_USE_VREFBUF
        "Drive VREF+ from the internal VREFBUF (2.048 V) instead of assuming VREF+ tied to VDDA"
        OFF)

option(NATALIA_ENABLE_TC1047_DRIVER
        "Build TC1047 analog temperature sensor driver"
        OFF)

option(NATALIA_ENABLE_USB_DEVICE_DRIVER
        "Build USB Device driver"
        OFF)

option(NATALIA_ENABLE_FTDI_DRIVER
        "Build FTDI (USART1) log/data output driver"
        OFF)

option(NATALIA_USB_CLOCK_SOURCE_HSI48_CRS
        "Use HSI48 with CRS as USB 48 MHz clock"
        OFF)

option(NATALIA_USB_CLOCK_SOURCE_HSE8_PLLSAI1
        "Use HSE 8 MHz with PLLSAI1Q as USB 48 MHz clock"
        OFF)

option(NATALIA_ENABLE_NAND_DRIVER
        "Build NAND driver"
        OFF)

option(NATALIA_QSPI_SAMPLE_SHIFT
        "Enable QUADSPI sample shift"
        OFF)

option(NATALIA_QSPI_DMA_READ
        "Use DMA automatically in qspi_read when transfer is aligned and large enough"
        ON)

option(NATALIA_QSPI_DMA_WRITE
        "Use DMA automatically in qspi_write when transfer is aligned and large enough"
        ON)

option(NATALIA_SPI_DMA_READ
        "Use DMA automatically in MRAM SPI reads when transfer is large enough"
        ON)

option(NATALIA_SPI_DMA_WRITE
        "Use DMA automatically in MRAM SPI writes when transfer is large enough"
        ON)

option(NATALIA_ENABLE_BOARD_TEST_HOOKS
        "Enable temporary board test hooks"
        OFF)

set(NATALIA_QSPI_TARGET_HZ "40000000" CACHE STRING
        "QUADSPI target clock frequency"
)

set(NATALIA_QSPI_DMA_AUTO_MIN_SIZE "16" CACHE STRING
        "Minimum transfer size for automatic QSPI DMA path"
)

set(NATALIA_SPI_DMA_AUTO_MIN_SIZE "16" CACHE STRING
        "Minimum transfer size for automatic MRAM SPI DMA path"
)

set(NATALIA_SPI_MRAM_BR "0" CACHE STRING
        "SPI baud-rate divider for MRAM (CR1 BR field): 0=PCLK/2=40MHz, 1=/4=20MHz, 2=/8=10MHz at 80MHz PCLK"
)
set_property(CACHE NATALIA_SPI_MRAM_BR PROPERTY STRINGS 0 1 2 3 4 5 6 7)

set(NATALIA_NAND_CACHE_READ_MODE "1" CACHE STRING
        "NAND cache read mode: 1, 2 or 4"
)

set(NATALIA_NAND_PROGRAM_LOAD_MODE "4" CACHE STRING
        "NAND program load mode: 1 or 4"
)

set(NATALIA_NAND_POWER_SWITCH "FPF2101" CACHE STRING
        "Installed NAND load switch: FPF2101 (enable active-low) or FPF2006 (enable active-high). Determines the PS OFF level automatically."
)

set(NATALIA_NAND_POWER_CONTROL "ON" CACHE STRING
        "NAND power control mode: ON uses PS/PSON pins, OFF assumes NAND is externally powered."
)

set(NATALIA_FIRMWARE_MAIN "main.c" CACHE STRING
        "Firmware main source file"
)

set_property(CACHE NATALIA_NAND_CACHE_READ_MODE PROPERTY STRINGS 1 2 4)
set_property(CACHE NATALIA_NAND_PROGRAM_LOAD_MODE PROPERTY STRINGS 1 4)
set_property(CACHE NATALIA_NAND_POWER_CONTROL PROPERTY STRINGS ON OFF)
set_property(CACHE NATALIA_NAND_POWER_SWITCH PROPERTY STRINGS FPF2101 FPF2006)

# Derive the electrical PS OFF level from the installed load switch part.
# FPF2101: enable is active-low  -> OFF level is HIGH.
# FPF2006: enable is active-high -> OFF level is LOW.
if (NATALIA_NAND_POWER_SWITCH STREQUAL "FPF2101")
    set(NATALIA_NAND_PS_OFF_LEVEL "HIGH")
elseif (NATALIA_NAND_POWER_SWITCH STREQUAL "FPF2006")
    set(NATALIA_NAND_PS_OFF_LEVEL "LOW")
else()
    message(FATAL_ERROR
            "Set -DNATALIA_NAND_POWER_SWITCH=FPF2101 or FPF2006"
    )
endif()

# -----------------------------------------------------------------------------
# Configuration checks
# -----------------------------------------------------------------------------

if (NATALIA_BUILD_TESTS AND NATALIA_ENABLE_DETECTOR_PROTO_LOG)
    message(FATAL_ERROR
            "NATALIA_ENABLE_DETECTOR_PROTO_LOG is available only for firmware builds"
    )
endif()

if (NOT NATALIA_LOG_BACKEND STREQUAL "NONE" AND
        NOT NATALIA_LOG_BACKEND STREQUAL "LPUART1" AND
        NOT NATALIA_LOG_BACKEND STREQUAL "USART2" AND
        NOT NATALIA_LOG_BACKEND STREQUAL "FTDI" AND
        NOT NATALIA_LOG_BACKEND STREQUAL "CAN")
    message(FATAL_ERROR
            "Set -DNATALIA_LOG_BACKEND=NONE, LPUART1, USART2, FTDI or CAN"
    )
endif()

if (NATALIA_LOG_BACKEND STREQUAL "FTDI")
    set(NATALIA_ENABLE_FTDI_DRIVER ON CACHE BOOL
            "Build FTDI (USART1) log/data output driver" FORCE)
endif()

if (NATALIA_ENABLE_FTDI_DRIVER AND NATALIA_ENABLE_USB_DEVICE_DRIVER)
    message(FATAL_ERROR
            "NATALIA_ENABLE_FTDI_DRIVER and NATALIA_ENABLE_USB_DEVICE_DRIVER are mutually exclusive (they share PA9/PA11/PA12)"
    )
endif()

if (NATALIA_ENABLE_FTDI_DRIVER AND NATALIA_LOG_BACKEND STREQUAL "LPUART1")
    message(FATAL_ERROR
            "NATALIA_ENABLE_FTDI_DRIVER conflicts with NATALIA_LOG_BACKEND=LPUART1 (they share PG8)"
    )
endif()

if (NATALIA_ENABLE_DETECTOR_PROTO_LOG AND NATALIA_LOG_BACKEND STREQUAL "NONE")
    message(FATAL_ERROR
            "NATALIA_ENABLE_DETECTOR_PROTO_LOG requires NATALIA_LOG_BACKEND != NONE"
    )
endif()

if (NATALIA_ENABLE_DETECTOR_PROTO_LOG AND NOT NATALIA_ENABLE_UNICAN_DRIVER)
    message(FATAL_ERROR
            "NATALIA_ENABLE_DETECTOR_PROTO_LOG requires NATALIA_ENABLE_UNICAN_DRIVER=ON"
    )
endif()

if (NATALIA_ENABLE_INA219_DRIVER AND NOT NATALIA_ENABLE_I2C_DRIVER)
    message(FATAL_ERROR
            "NATALIA_ENABLE_INA219_DRIVER requires NATALIA_ENABLE_I2C_DRIVER=ON"
    )
endif()

if (NATALIA_ENABLE_TC1047_DRIVER AND NOT NATALIA_ENABLE_ADC_DRIVER)
    message(FATAL_ERROR
            "NATALIA_ENABLE_TC1047_DRIVER requires NATALIA_ENABLE_ADC_DRIVER=ON"
    )
endif()

if (NATALIA_ENABLE_MRAM_DRIVER AND NOT NATALIA_ENABLE_SPI_DRIVER)
    message(FATAL_ERROR
            "NATALIA_ENABLE_MRAM_DRIVER requires NATALIA_ENABLE_SPI_DRIVER=ON"
    )
endif()

if (NOT NATALIA_NAND_POWER_CONTROL STREQUAL "ON" AND
        NOT NATALIA_NAND_POWER_CONTROL STREQUAL "OFF")
    message(FATAL_ERROR
            "Set -DNATALIA_NAND_POWER_CONTROL=ON or OFF"
    )
endif()

if (NOT NATALIA_NAND_CACHE_READ_MODE STREQUAL "1" AND
        NOT NATALIA_NAND_CACHE_READ_MODE STREQUAL "2" AND
        NOT NATALIA_NAND_CACHE_READ_MODE STREQUAL "4")
    message(FATAL_ERROR
            "Set -DNATALIA_NAND_CACHE_READ_MODE=1, 2 or 4"
    )
endif()

if (NOT NATALIA_NAND_PROGRAM_LOAD_MODE STREQUAL "1" AND
        NOT NATALIA_NAND_PROGRAM_LOAD_MODE STREQUAL "4")
    message(FATAL_ERROR
            "Set -DNATALIA_NAND_PROGRAM_LOAD_MODE=1 or 4"
    )
endif()

if (NATALIA_BUILD_TESTS AND NOT NATALIA_USE_BOARD_STUBS)
    message(FATAL_ERROR
            "Desktop tests require -DNATALIA_USE_BOARD_STUBS=ON"
    )
endif()

if (NATALIA_BUILD_TESTS AND NATALIA_ENABLE_USB_DEVICE_DRIVER)
    message(FATAL_ERROR
            "NATALIA_ENABLE_USB_DEVICE_DRIVER is available only for firmware builds"
    )
endif()

if (NATALIA_USB_CLOCK_SOURCE_HSI48_CRS AND NATALIA_USB_CLOCK_SOURCE_HSE8_PLLSAI1)
    message(FATAL_ERROR
            "Select only one USB clock source"
    )
endif()

if (NATALIA_ENABLE_USB_DEVICE_DRIVER AND
        NOT NATALIA_USB_CLOCK_SOURCE_HSI48_CRS AND
        NOT NATALIA_USB_CLOCK_SOURCE_HSE8_PLLSAI1)
    message(FATAL_ERROR
            "NATALIA_ENABLE_USB_DEVICE_DRIVER requires one USB clock source"
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
if (NATALIA_ENABLE_USB_DEVICE_DRIVER)
    add_subdirectory(Drivers/USB_Device)
endif()
add_subdirectory(BSP)
add_subdirectory(Drivers)

if (TARGET natalia_board_api_impl AND TARGET natalia_nand)
    target_link_libraries(natalia_board_api_impl PUBLIC
            natalia_nand
    )
endif()

if (TARGET natalia_board_api_impl AND TARGET natalia_ped_reg)
    target_link_libraries(natalia_board_api_impl PUBLIC
            natalia_ped_reg
    )

    target_compile_definitions(natalia_board_api_impl PRIVATE
            NATALIA_ENABLE_PED_REG_DRIVER=1
    )
endif()

if (TARGET natalia_board_api_impl AND TARGET natalia_tc1047)
    target_link_libraries(natalia_board_api_impl PUBLIC
            natalia_tc1047
    )
endif()

if (TARGET natalia_board_api_impl AND TARGET natalia_ina219)
    target_link_libraries(natalia_board_api_impl PUBLIC
            natalia_ina219
    )

    target_compile_definitions(natalia_board_api_impl PRIVATE
            NATALIA_ENABLE_INA219_DRIVER=1
    )
endif()

if (TARGET natalia_board_api_impl AND TARGET natalia_unican)
    target_link_libraries(natalia_board_api_impl PUBLIC
            natalia_unican
    )

    target_compile_definitions(natalia_board_api_impl PRIVATE
            NATALIA_ENABLE_UNICAN_DRIVER=1
    )
endif()

if (TARGET natalia_board_api_impl AND TARGET natalia_tmp112)
    target_link_libraries(natalia_board_api_impl PUBLIC
            natalia_tmp112
    )

    target_compile_definitions(natalia_board_api_impl PRIVATE
            NATALIA_ENABLE_TMP112_DRIVER=1
    )
endif()

if (TARGET natalia_board_api_impl AND TARGET natalia_mram)
    target_link_libraries(natalia_board_api_impl PUBLIC
            natalia_mram
            natalia_core
    )

    target_compile_definitions(natalia_board_api_impl PRIVATE
            NATALIA_ENABLE_MRAM_DRIVER=1
    )
endif()

if (NATALIA_ENABLE_DETECTOR_PROTO_LOG)
    add_subdirectory(Diagnostics)
endif()

add_subdirectory(Algorithm)

# -----------------------------------------------------------------------------
# Firmware executable
# -----------------------------------------------------------------------------

if (IS_ABSOLUTE "${NATALIA_FIRMWARE_MAIN}")
    set(NATALIA_FIRMWARE_MAIN_SOURCE "${NATALIA_FIRMWARE_MAIN}")
else()
    set(NATALIA_FIRMWARE_MAIN_SOURCE "${CMAKE_SOURCE_DIR}/${NATALIA_FIRMWARE_MAIN}")
endif()

add_executable(natalia_firmware
        ${NATALIA_FIRMWARE_MAIN_SOURCE}
        Device/src/syscalls.c
        Device/src/sysmem.c
)

target_include_directories(natalia_firmware PRIVATE
        ${CMAKE_SOURCE_DIR}/Algorithm/include
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