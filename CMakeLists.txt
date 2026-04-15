cmake_minimum_required(VERSION 3.14)
project(picmerge_c99 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

include(CTest)

if(MSVC)
    add_compile_options(/W3 /utf-8)
    add_compile_options($<$<CONFIG:Release>:/O2>)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    add_compile_options($<$<CONFIG:Release>:-O2>)
endif()

add_library(picmerge_c99_lib
    src/image_io.c
    src/row_signature.c
    src/bar_detector.c
    src/sticky_header.c
    src/overlap_finder.c
    src/stitcher.c
)

target_include_directories(picmerge_c99_lib PUBLIC
    src
    third_party/stb
)

add_executable(picmerge_c99
    src/main.c
)
target_link_libraries(picmerge_c99 PRIVATE picmerge_c99_lib)

if(BUILD_TESTING)
    add_executable(picmerge_c99_tests
        tests/picmerge_tests.c
    )
    target_link_libraries(picmerge_c99_tests PRIVATE picmerge_c99_lib)
    target_compile_definitions(picmerge_c99_tests PRIVATE
        PICMERGE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )
    add_test(NAME picmerge_c99_tests COMMAND picmerge_c99_tests)
endif()
