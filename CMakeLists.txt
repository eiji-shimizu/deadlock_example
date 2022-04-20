cmake_minimum_required(VERSION 3.19.6)
project(deadlock_example CXX)

set(DEADLOCK_EXAMPLE_INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/include)

# Releaseビルド時 cmake -DCMAKE_BUILD_TYPE=Release ..
# テストビルド時 cmake -DCMAKE_BUILD_TYPE=Debug ..
if(${CMAKE_BUILD_TYPE} STREQUAL "Release")
    message("---------------------- Release Build")
    set(COMPILE_OPTIONS /W4 /EHsc /MD /utf-8 /std:c++17)
    set(COMPILE_DEFINITIONS NDEBUG)
else()
    message("---------------------- Not Release Build")
    #enable_testing()
    set(COMPILE_OPTIONS /W4 /EHa /MDd /utf-8 /std:c++17)
    #add_subdirectory(test)
endif()

# サブディレクトリを登録
add_subdirectory(src)
