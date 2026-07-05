add_library(natalia_drivers INTERFACE)

if (NATALIA_ENABLE_PED_REG_DRIVER)
    add_subdirectory(PED_REG)

    target_link_libraries(natalia_drivers INTERFACE
            natalia_ped_reg
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