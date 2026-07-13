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