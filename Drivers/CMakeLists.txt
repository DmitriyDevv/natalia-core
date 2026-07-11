add_library(natalia_drivers INTERFACE)

option(NATALIA_ENABLE_TMP112_DRIVER
        "Build TMP112 digital temperature sensor driver"
        OFF)

if (NATALIA_ENABLE_PED_REG_DRIVER)
    add_subdirectory(PED_REG)

    target_link_libraries(natalia_drivers INTERFACE
            natalia_ped_reg
    )
endif()

if (NATALIA_ENABLE_INA219_DRIVER)
    if (NOT NATALIA_ENABLE_I2C_DRIVER)
        message(FATAL_ERROR
                "NATALIA_ENABLE_INA219_DRIVER requires NATALIA_ENABLE_I2C_DRIVER=ON"
        )
    endif()

    add_subdirectory(INA219)

    target_link_libraries(natalia_drivers INTERFACE
            natalia_ina219
    )
endif()

if (NATALIA_ENABLE_TC1047_DRIVER)
    add_subdirectory(Temp/TC1047)

    target_link_libraries(natalia_drivers INTERFACE
            natalia_tc1047
    )
endif()

if (NATALIA_ENABLE_TMP112_DRIVER)
    if (NOT NATALIA_ENABLE_I2C_DRIVER)
        message(FATAL_ERROR
                "NATALIA_ENABLE_TMP112_DRIVER requires NATALIA_ENABLE_I2C_DRIVER=ON"
        )
    endif()

    add_subdirectory(Temp/TMP112)

    target_link_libraries(natalia_drivers INTERFACE
            natalia_tmp112
    )
endif()

if (NATALIA_ENABLE_UNICAN_DRIVER)
    add_subdirectory(UniCAN)

    target_link_libraries(natalia_drivers INTERFACE
            natalia_unican
    )
endif()

if (NATALIA_ENABLE_USB_DEVICE_DRIVER)
    if (NOT TARGET natalia_usb_otg_fs)
        message(FATAL_ERROR
                "NATALIA_ENABLE_USB_DEVICE_DRIVER requires target natalia_usb_otg_fs"
        )
    endif()

    target_link_libraries(natalia_drivers INTERFACE
            natalia_usb_otg_fs
    )
endif()

if (NATALIA_ENABLE_NAND_DRIVER)
    add_subdirectory(NAND)

    target_link_libraries(natalia_drivers INTERFACE
            natalia_nand
    )
endif()