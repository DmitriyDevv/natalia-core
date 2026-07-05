add_library(natalia_usb_device STATIC
        src/usb_dev.c
        src/usb_class.c
)

target_include_directories(natalia_usb_device PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/BSP/USB_OTG_FS/include
)

target_link_libraries(natalia_usb_device PUBLIC
        natalia_cmsis
)

target_compile_definitions(natalia_usb_device PUBLIC
        SIMPLE_CDC=1
)

target_compile_options(natalia_usb_device PRIVATE
        -Wall
        -Wextra
        -ffunction-sections
        -fdata-sections
)