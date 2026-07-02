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

if (NATALIA_ENABLE_DEBUG_LOG)
    target_sources(natalia_bsp PRIVATE
            UART/src/lpuart1_config.c
            UART/src/lpuart1.c
    )

    target_compile_definitions(natalia_bsp PUBLIC
            NATALIA_ENABLE_DEBUG_LOG=1
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

if(NATALIA_ENABLE_BOARD_TEST_HOOKS)
    target_compile_definitions(natalia_board_api_impl PUBLIC
            NATALIA_ENABLE_BOARD_TEST_HOOKS=1
    )
endif()