# Host tests rely on assert() to check behavior. The project forces
# CMAKE_BUILD_TYPE=RelWithDebInfo, which defines NDEBUG and would compile every
# assert() to nothing. Undefine NDEBUG for all test targets so assertions run
# regardless of the inherited build type.
add_compile_options(-UNDEBUG)

add_executable(test_state_machine
        test_state_machine.c
)

target_link_libraries(test_state_machine PRIVATE
        natalia_algorithm
)

target_compile_options(test_state_machine PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_test(NAME test_state_machine COMMAND test_state_machine)

add_executable(test_transport
        test_transport.c
)

target_link_libraries(test_transport PRIVATE
        natalia_algorithm
)

target_compile_options(test_transport PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_test(NAME test_transport COMMAND test_transport)

add_executable(test_observe_mode
        test_observe_mode.c
)

target_link_libraries(test_observe_mode PRIVATE
        natalia_algorithm
)

target_compile_options(test_observe_mode PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_test(NAME test_observe_mode COMMAND test_observe_mode)

add_executable(test_mram_store
        test_mram_store.c
)

target_link_libraries(test_mram_store PRIVATE
        natalia_algorithm
)

target_compile_options(test_mram_store PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_test(NAME test_mram_store COMMAND test_mram_store)

add_executable(test_set_cfg
        test_set_cfg.c
)

target_link_libraries(test_set_cfg PRIVATE
        natalia_algorithm
)

target_compile_options(test_set_cfg PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_test(NAME test_set_cfg COMMAND test_set_cfg)

add_executable(test_mode_test
        test_mode_test.c
)

target_link_libraries(test_mode_test PRIVATE
        natalia_algorithm
)

target_compile_options(test_mode_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_test(NAME test_mode_test COMMAND test_mode_test)

add_executable(test_shutdown
        test_shutdown.c
)

target_link_libraries(test_shutdown PRIVATE
        natalia_algorithm
)

target_compile_options(test_shutdown PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_test(NAME test_shutdown COMMAND test_shutdown)

add_executable(test_erase
        test_erase.c
)

target_link_libraries(test_erase PRIVATE
        natalia_algorithm
)

target_compile_options(test_erase PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_test(NAME test_erase COMMAND test_erase)

add_executable(test_mode_alarms
        test_mode_alarms.c
)

target_link_libraries(test_mode_alarms PRIVATE
        natalia_algorithm
)

target_compile_options(test_mode_alarms PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_test(NAME test_mode_alarms COMMAND test_mode_alarms)

add_executable(test_dump
        test_dump.c
)

target_link_libraries(test_dump PRIVATE
        natalia_algorithm
)

target_compile_options(test_dump PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_test(NAME test_dump COMMAND test_dump)

add_executable(test_admissibility
        test_admissibility.c
)

target_link_libraries(test_admissibility PRIVATE
        natalia_algorithm
)

target_compile_options(test_admissibility PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_test(NAME test_admissibility COMMAND test_admissibility)

add_executable(test_hw_events
        test_hw_events.c
)

target_link_libraries(test_hw_events PRIVATE
        natalia_algorithm
)

target_compile_options(test_hw_events PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_test(NAME test_hw_events COMMAND test_hw_events)

add_executable(test_alarm_monitor
        test_alarm_monitor.c
)

target_link_libraries(test_alarm_monitor PRIVATE
        natalia_algorithm
)

target_compile_options(test_alarm_monitor PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_test(NAME test_alarm_monitor COMMAND test_alarm_monitor)
add_executable(test_ni_format
        test_ni_format.c
)

target_link_libraries(test_ni_format PRIVATE
        natalia_algorithm
)

target_compile_options(test_ni_format PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_test(NAME test_ni_format COMMAND test_ni_format)

add_executable(test_ni_stream
        test_ni_stream.c
)

target_link_libraries(test_ni_stream PRIVATE
        natalia_algorithm
)

target_compile_options(test_ni_stream PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_test(NAME test_ni_stream COMMAND test_ni_stream)

add_executable(test_observe_science
        test_observe_science.c
)

target_link_libraries(test_observe_science PRIVATE
        natalia_algorithm
)

target_compile_options(test_observe_science PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_test(NAME test_observe_science COMMAND test_observe_science)

add_executable(test_ped_ring
        test_ped_ring.c
        ${CMAKE_SOURCE_DIR}/Drivers/PED_REG/src/ped_reg_ring.c
)

target_include_directories(test_ped_ring PRIVATE
        ${CMAKE_SOURCE_DIR}/Drivers/PED_REG/include
)

target_link_libraries(test_ped_ring PRIVATE
        natalia_algorithm
)

target_compile_options(test_ped_ring PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_test(NAME test_ped_ring COMMAND test_ped_ring)

add_executable(test_ni_writer
        test_ni_writer.c
)

target_link_libraries(test_ni_writer PRIVATE
        natalia_algorithm
)

target_compile_options(test_ni_writer PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_test(NAME test_ni_writer COMMAND test_ni_writer)

add_executable(test_nand_map
        test_nand_map.c
)

target_link_libraries(test_nand_map PRIVATE
        natalia_algorithm
)

target_compile_options(test_nand_map PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_test(NAME test_nand_map COMMAND test_nand_map)

# nand_storage has no register access, so it is compiled here against a fake
# nand_mt29f (tests/fake_nand_mt29f.c) instead of the real driver.
add_executable(test_nand_storage
        test_nand_storage.c
        fake_nand_mt29f.c
        ${CMAKE_SOURCE_DIR}/Drivers/NAND/src/nand_storage.c
)

target_include_directories(test_nand_storage PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/Drivers/NAND/include
)

target_link_libraries(test_nand_storage PRIVATE
        natalia_core_api
)

target_compile_options(test_nand_storage PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_test(NAME test_nand_storage COMMAND test_nand_storage)
