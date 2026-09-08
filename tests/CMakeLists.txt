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

add_executable(test_observe
        test_observe.c
)

target_link_libraries(test_observe PRIVATE
        natalia_algorithm
)

target_compile_options(test_observe PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_test(NAME test_observe COMMAND test_observe)

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

add_executable(test_event_queue
        test_event_queue.c
)

target_link_libraries(test_event_queue PRIVATE
        natalia_algorithm
)

target_compile_options(test_event_queue PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_test(NAME test_event_queue COMMAND test_event_queue)

add_executable(test_alarm_mode
        test_alarm_mode.c
)

target_link_libraries(test_alarm_mode PRIVATE
        natalia_algorithm
)

target_compile_options(test_alarm_mode PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_test(NAME test_alarm_mode COMMAND test_alarm_mode)

add_executable(test_duty
        test_duty.c
)

target_link_libraries(test_duty PRIVATE
        natalia_algorithm
)

target_compile_options(test_duty PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_test(NAME test_duty COMMAND test_duty)

add_executable(test_nand_full
        test_nand_full.c
)

target_link_libraries(test_nand_full PRIVATE
        natalia_algorithm
)

target_compile_options(test_nand_full PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_test(NAME test_nand_full COMMAND test_nand_full)

add_executable(test_mram_recovery
        test_mram_recovery.c
)

target_link_libraries(test_mram_recovery PRIVATE
        natalia_algorithm
)

target_compile_options(test_mram_recovery PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_test(NAME test_mram_recovery COMMAND test_mram_recovery)
