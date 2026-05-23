add_library(natalia_cmsis INTERFACE)

target_include_directories(natalia_cmsis SYSTEM INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/Include
        ${CMAKE_CURRENT_SOURCE_DIR}/Device/ST/STM32L4xx/Include
)

target_compile_definitions(natalia_cmsis INTERFACE
        STM32L496xx
)