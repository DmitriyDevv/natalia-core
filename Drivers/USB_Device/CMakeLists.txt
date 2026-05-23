add_library(natalia_usb_device STATIC
        src/usb_device.c
        src/usb_device_config.c
)

target_include_directories(natalia_usb_device PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(natalia_usb_device PUBLIC
        natalia_core_api
        natalia_device
        natalia_bsp
        natalia_cmsis
)

target_compile_options(natalia_usb_device PRIVATE
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