enable_testing()

add_executable(
    de_test
    IdGeneratorTest.cpp
    Setup.cpp
)
target_link_libraries(
    de_test
    gtest_main
)

target_include_directories(de_test
                            PRIVATE ${DEADLOCK_EXAMPLE_INCLUDE_DIRS})

# compile options
target_compile_options(de_test PUBLIC ${COMPILE_OPTIONS})

# compile definitions
target_compile_definitions(de_test PUBLIC ${COMPILE_DEFINITIONS})

include(GoogleTest)
gtest_discover_tests(de_test)
