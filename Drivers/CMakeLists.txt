add_library(natalia_drivers INTERFACE)

option(NATALIA_ENABLE_PED_REG_DRIVER "Build PED_REG driver" OFF)
option(NATALIA_ENABLE_UNICAN_DRIVER "Build UniCAN driver" OFF)
option(NATALIA_ENABLE_USB_DEVICE_DRIVER "Build USB Device driver" OFF)

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
    add_subdirectory(USB_Device)

    target_link_libraries(natalia_drivers INTERFACE
            natalia_usb_device
    )
endif()