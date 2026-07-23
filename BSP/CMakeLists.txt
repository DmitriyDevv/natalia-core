option(NATALIA_ENABLE_I2C_DRIVER
        "Build BSP I2C master driver"
        OFF)

add_library(natalia_bsp STATIC
        GPIO/src/gpio.c
        Board_API/src/board_startup_io.c
        QSPI/src/qspi_config.c
        QSPI/src/qspi.c
        RTC/src/rtc_config.c
        RTC/src/rtc.c
        CAN/src/can1_config.c
        CAN/src/can1.c
        UART/src/debug_log.c
)

target_include_directories(natalia_bsp PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/GPIO/include
        ${CMAKE_CURRENT_SOURCE_DIR}/ADC/include
        ${CMAKE_CURRENT_SOURCE_DIR}/Board_API/include
        ${CMAKE_CURRENT_SOURCE_DIR}/CAN/include
        ${CMAKE_CURRENT_SOURCE_DIR}/I2C/include
        ${CMAKE_CURRENT_SOURCE_DIR}/QSPI/include
        ${CMAKE_CURRENT_SOURCE_DIR}/RTC/include
        ${CMAKE_CURRENT_SOURCE_DIR}/SPI/include
        ${CMAKE_CURRENT_SOURCE_DIR}/UART/include
        ${CMAKE_CURRENT_SOURCE_DIR}/USB_OTG_FS/include
)

target_link_libraries(natalia_bsp PUBLIC
        natalia_core_api
        natalia_device
        natalia_cmsis
)

target_compile_definitions(natalia_bsp PUBLIC
        QSPI_TARGET_HZ=${NATALIA_QSPI_TARGET_HZ}
        QSPI_DMA_AUTO_MIN_SIZE=${NATALIA_QSPI_DMA_AUTO_MIN_SIZE}
)

if (NATALIA_ENABLE_I2C_DRIVER)
    target_sources(natalia_bsp PRIVATE
            I2C/src/i2c.c
    )

    target_compile_definitions(natalia_bsp PUBLIC
            NATALIA_ENABLE_I2C_DRIVER=1
    )
endif()

if (NATALIA_ENABLE_SPI_DRIVER)
    target_sources(natalia_bsp PRIVATE
            SPI/src/spi.c
    )

    target_compile_definitions(natalia_bsp PUBLIC
            NATALIA_ENABLE_SPI_DRIVER=1
            NATALIA_SPI_MRAM_BR=${NATALIA_SPI_MRAM_BR}
            SPI_DMA_AUTO_MIN_SIZE=${NATALIA_SPI_DMA_AUTO_MIN_SIZE}
    )

    if (NATALIA_SPI_DMA_READ)
        target_compile_definitions(natalia_bsp PUBLIC
                SPI_DMA_READ_ENABLED=1
        )
    else()
        target_compile_definitions(natalia_bsp PUBLIC
                SPI_DMA_READ_ENABLED=0
        )
    endif()

    if (NATALIA_SPI_DMA_WRITE)
        target_compile_definitions(natalia_bsp PUBLIC
                SPI_DMA_WRITE_ENABLED=1
        )
    else()
        target_compile_definitions(natalia_bsp PUBLIC
                SPI_DMA_WRITE_ENABLED=0
        )
    endif()
endif()

if (NATALIA_ENABLE_ADC_DRIVER)
    target_sources(natalia_bsp PRIVATE
            ADC/src/adc.c
    )

    target_compile_definitions(natalia_bsp PUBLIC
            NATALIA_ENABLE_ADC_DRIVER=1
    )

    if (NATALIA_ADC_USE_VREFBUF)
        target_compile_definitions(natalia_bsp PRIVATE
                NATALIA_ADC_USE_VREFBUF=1
        )
    endif()
endif()

if (NATALIA_QSPI_SAMPLE_SHIFT)
    target_compile_definitions(natalia_bsp PUBLIC
            QSPI_SAMPLE_SHIFT=1
    )
else()
    target_compile_definitions(natalia_bsp PUBLIC
            QSPI_SAMPLE_SHIFT=0
    )
endif()

if (NATALIA_QSPI_DMA_READ)
    target_compile_definitions(natalia_bsp PUBLIC
            QSPI_DMA_READ_ENABLED=1
    )
else()
    target_compile_definitions(natalia_bsp PUBLIC
            QSPI_DMA_READ_ENABLED=0
    )
endif()

if (NATALIA_QSPI_DMA_WRITE)
    target_compile_definitions(natalia_bsp PUBLIC
            QSPI_DMA_WRITE_ENABLED=1
    )
else()
    target_compile_definitions(natalia_bsp PUBLIC
            QSPI_DMA_WRITE_ENABLED=0
    )
endif()

if (NATALIA_NAND_PS_OFF_LEVEL STREQUAL "HIGH")
    target_compile_definitions(natalia_bsp PRIVATE
            NATALIA_NAND_PS_OFF_LEVEL=1
    )
elseif (NATALIA_NAND_PS_OFF_LEVEL STREQUAL "LOW")
    target_compile_definitions(natalia_bsp PRIVATE
            NATALIA_NAND_PS_OFF_LEVEL=0
    )
else()
    message(FATAL_ERROR
            "Set -DNATALIA_NAND_PS_OFF_LEVEL=HIGH or LOW according to the installed NAND power switch."
    )
endif()

if (NATALIA_NAND_POWER_CONTROL STREQUAL "ON")
    target_compile_definitions(natalia_bsp PRIVATE
            NATALIA_NAND_POWER_CONTROL=1
    )
elseif (NATALIA_NAND_POWER_CONTROL STREQUAL "OFF")
    target_compile_definitions(natalia_bsp PRIVATE
            NATALIA_NAND_POWER_CONTROL=0
    )
else()
    message(FATAL_ERROR
            "Set -DNATALIA_NAND_POWER_CONTROL=ON or OFF."
    )
endif()

if (NATALIA_LOG_BACKEND STREQUAL "NONE")
    target_compile_definitions(natalia_bsp PUBLIC
            NATALIA_LOG_BACKEND=0
    )
elseif (NATALIA_LOG_BACKEND STREQUAL "LPUART1")
    target_sources(natalia_bsp PRIVATE
            UART/src/lpuart1_config.c
            UART/src/lpuart1.c
            UART/src/log_backend_lpuart1.c
    )

    target_compile_definitions(natalia_bsp PUBLIC
            NATALIA_LOG_BACKEND=1
    )
elseif (NATALIA_LOG_BACKEND STREQUAL "USART2")
    target_sources(natalia_bsp PRIVATE
            UART/src/usart2_config.c
            UART/src/usart2.c
            UART/src/log_backend_usart2.c
    )

    target_compile_definitions(natalia_bsp PUBLIC
            NATALIA_LOG_BACKEND=2
    )
elseif (NATALIA_LOG_BACKEND STREQUAL "FTDI")
    target_sources(natalia_bsp PRIVATE
            UART/src/log_backend_ftdi.c
    )

    target_compile_definitions(natalia_bsp PUBLIC
            NATALIA_LOG_BACKEND=3
    )
elseif (NATALIA_LOG_BACKEND STREQUAL "CAN")
    target_sources(natalia_bsp PRIVATE
            UART/src/log_backend_can.c
    )

    target_compile_definitions(natalia_bsp PUBLIC
            NATALIA_LOG_BACKEND=4
    )
else()
    message(FATAL_ERROR
            "Unsupported NATALIA_LOG_BACKEND '${NATALIA_LOG_BACKEND}' for natalia_bsp"
    )
endif()

if (NATALIA_ENABLE_FTDI_DRIVER)
    target_sources(natalia_bsp PRIVATE
            UART/src/ftdi.c
    )

    target_compile_definitions(natalia_bsp PUBLIC
            NATALIA_ENABLE_FTDI_DRIVER=1
    )
endif()

target_compile_options(natalia_bsp PRIVATE
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

if (NATALIA_ENABLE_USB_DEVICE_DRIVER)
    if (NOT TARGET natalia_usb_device)
        message(FATAL_ERROR
                "NATALIA_ENABLE_USB_DEVICE_DRIVER requires target natalia_usb_device"
        )
    endif()

    add_library(natalia_usb_otg_fs STATIC
            USB_OTG_FS/src/usb_hw_l4.c
            USB_OTG_FS/src/usb_cdc.c
            ${CMAKE_SOURCE_DIR}/Drivers/USB_Device/src/usb_app_simple_cdc.c
    )

    target_include_directories(natalia_usb_otg_fs PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/USB_OTG_FS/include
            ${CMAKE_SOURCE_DIR}/Drivers/USB_Device/include
    )

    target_link_libraries(natalia_usb_otg_fs PUBLIC
            natalia_core_api
            natalia_device
            natalia_bsp
            natalia_cmsis
            natalia_usb_device
    )

    if (NATALIA_USB_CLOCK_SOURCE_HSI48_CRS)
        target_compile_definitions(natalia_usb_otg_fs PRIVATE
                NATALIA_USB_CLOCK_SOURCE_HSI48_CRS=1
                NATALIA_USB_CLOCK_SOURCE_HSE8_PLLSAI1=0
        )
    elseif (NATALIA_USB_CLOCK_SOURCE_HSE8_PLLSAI1)
        target_compile_definitions(natalia_usb_otg_fs PRIVATE
                NATALIA_USB_CLOCK_SOURCE_HSI48_CRS=0
                NATALIA_USB_CLOCK_SOURCE_HSE8_PLLSAI1=1
        )
    endif()

    target_compile_options(natalia_usb_otg_fs PRIVATE
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
endif()

if (NOT NATALIA_USE_BOARD_STUBS)
    add_library(natalia_board_api_impl STATIC
            Board_API/src/board_api.c
    )

    target_link_libraries(natalia_board_api_impl PUBLIC
            natalia_core_api
            natalia_bsp
    )

    if (NATALIA_NAND_PS_OFF_LEVEL STREQUAL "HIGH")
        target_compile_definitions(natalia_board_api_impl PRIVATE
                NATALIA_NAND_PS_OFF_LEVEL=1
        )
    elseif (NATALIA_NAND_PS_OFF_LEVEL STREQUAL "LOW")
        target_compile_definitions(natalia_board_api_impl PRIVATE
                NATALIA_NAND_PS_OFF_LEVEL=0
        )
    endif()

    if (NATALIA_NAND_POWER_CONTROL STREQUAL "ON")
        target_compile_definitions(natalia_board_api_impl PRIVATE
                NATALIA_NAND_POWER_CONTROL=1
        )
    elseif (NATALIA_NAND_POWER_CONTROL STREQUAL "OFF")
        target_compile_definitions(natalia_board_api_impl PRIVATE
                NATALIA_NAND_POWER_CONTROL=0
        )
    endif()

    if (NATALIA_ENABLE_NAND_DRIVER)
        target_compile_definitions(natalia_board_api_impl PRIVATE
                NATALIA_ENABLE_NAND_DRIVER=1
        )
    endif()

    if (NATALIA_ENABLE_TC1047_DRIVER)
        target_compile_definitions(natalia_board_api_impl PRIVATE
                NATALIA_ENABLE_TC1047_DRIVER=1
        )
    endif()

    if (NATALIA_ENABLE_USB_DEVICE_DRIVER)
        target_compile_definitions(natalia_board_api_impl PRIVATE
                NATALIA_ENABLE_USB_DEVICE_DRIVER=1
        )

        target_link_libraries(natalia_board_api_impl PUBLIC
                natalia_usb_otg_fs
        )
    endif()

    if (NATALIA_ENABLE_FTDI_DRIVER)
        target_compile_definitions(natalia_board_api_impl PRIVATE
                NATALIA_ENABLE_FTDI_DRIVER=1
        )
    endif()

    if (NATALIA_ENABLE_BOARD_TEST_HOOKS)
        target_compile_definitions(natalia_board_api_impl PUBLIC
                NATALIA_ENABLE_BOARD_TEST_HOOKS=1
        )
    endif()

    target_compile_options(natalia_board_api_impl PRIVATE
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
endif()