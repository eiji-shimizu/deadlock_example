# ibc
add_executable(deadlock_example
                Main.cpp)

target_include_directories(deadlock_example
                            PRIVATE ${DEADLOCK_EXAMPLE_INCLUDE_DIRS})

# compile options
target_compile_options(deadlock_example PUBLIC ${COMPILE_OPTIONS})

# compile definitions
target_compile_definitions(deadlock_example PUBLIC ${COMPILE_DEFINITIONS})
