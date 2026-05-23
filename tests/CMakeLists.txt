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