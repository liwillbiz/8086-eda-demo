cmake_minimum_required(VERSION 3.25)
project(i8086_eda LANGUAGES CXX VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(I86_WERROR "Treat warnings as errors in project targets" OFF)
option(I86_ASAN "Build with AddressSanitizer and UBSan" OFF)
option(I86_BUILD_TESTS "Build unit tests" ON)

include(cmake/Dependencies.cmake)
include(cmake/I86Helpers.cmake)

if(I86_BUILD_TESTS)
  enable_testing()
  include(GoogleTest)
endif()

add_subdirectory(libs/common)
add_subdirectory(libs/spec)
add_subdirectory(libs/model)
add_subdirectory(libs/tester)
add_subdirectory(libs/golden)
add_subdirectory(testprog)
