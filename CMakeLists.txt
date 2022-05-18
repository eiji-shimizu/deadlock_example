cmake_minimum_required(VERSION 3.19.6)
project(deadlock_example CXX)
set(CMAKE_CXX_STANDARD 11)

set(DEADLOCK_EXAMPLE_INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/include)

# Releaseビルド時 cmake -DCMAKE_BUILD_TYPE=Release ..
# テストビルド時 cmake -DCMAKE_BUILD_TYPE=Debug ..
if(${CMAKE_BUILD_TYPE} STREQUAL "Release")
    message("---------------------- Release Build")
    set(COMPILE_OPTIONS /W4 /EHsc /MD /utf-8 /std:c++17)
    set(COMPILE_DEFINITIONS NDEBUG)
    if(DE_TEST)
        message("---------------------- de_test Build")
        include(FetchContent)
        FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/609281088cfefc76f9d0ce82e1ff6c30cc3591e5.zip
        )
        if(WIN32)
            # For Windows: Prevent overriding the parent project's compiler/linker settings
            set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        endif()
        FetchContent_MakeAvailable(googletest)
        add_subdirectory(test)
        unset(DE_TEST CACHE)
    else()
        message("---------------------- deadlock_example Build")
        add_subdirectory(src)
    endif()
else()
    message("---------------------- Not Release Build")
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/609281088cfefc76f9d0ce82e1ff6c30cc3591e5.zip
    )
    if(WIN32)
        # For Windows: Prevent overriding the parent project's compiler/linker settings
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    endif()
    FetchContent_MakeAvailable(googletest)
    set(COMPILE_OPTIONS /W4 /EHa /MDd /utf-8 /std:c++17)
    add_subdirectory(test)
endif()
