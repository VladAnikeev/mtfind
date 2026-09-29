cmake_minimum_required(VERSION 3.30)
project(test_assignment CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_TESTS "Build unit tests" ON)

add_library(mtfind_core STATIC
    mtfind_core.cpp
    mtfind_core.h
)

target_include_directories(mtfind_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Boost REQUIRED COMPONENTS iostreams program_options)

add_executable(mtfind main.cpp)
target_link_libraries(mtfind PRIVATE mtfind_core Boost::iostreams Boost::program_options)


if (BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)

    add_executable(mtfind_tests tests/test_mtfind_core.cpp)
    target_link_libraries(mtfind_tests PRIVATE mtfind_core GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(mtfind_tests)
endif()