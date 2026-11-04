# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "Host tests require a native Linux build (WSL is supported)")
endif()

find_package(Threads REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_library(host_test_config INTERFACE)
set(CMAKE_CXX_STANDARD 98)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
target_compile_definitions(host_test_config INTERFACE NBT_HOST_TEST)
# Tests use assert() for checks and setup, including in Release builds.
target_compile_options(host_test_config INTERFACE -Wall -Wextra -UNDEBUG)
target_include_directories(host_test_config INTERFACE
    "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(host_test_config INTERFACE Threads::Threads)

# Static archive: each test links only the members it references.
# aa_session is built per test, since the lifecycle tests stub the cryptor.
file(GLOB_RECURSE host_srcs CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/*.cpp" "${PROJECT_SOURCE_DIR}/src/*.c")
list(FILTER host_srcs EXCLUDE REGEX "/(main|aa_cryptor|aa_session|video_manager|asound_device)\\.cpp$")
add_library(host_support STATIC ${host_srcs})
target_link_libraries(host_support PUBLIC host_test_config)

file(GLOB cpp_tests CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp")
foreach(source IN LISTS cpp_tests)
    get_filename_component(test "${source}" NAME_WE)
    add_executable(${test} "${source}")
    target_link_libraries(${test} PRIVATE host_test_config)
    target_link_libraries(${test} PRIVATE host_support)
    if(test STREQUAL "test_session_lifecycle")
        target_sources(${test} PRIVATE "${PROJECT_SOURCE_DIR}/src/aa_session.cpp")
    endif()
    if(test STREQUAL "test_session_lifecycle")
        target_compile_options(${test} PRIVATE -include "${CMAKE_CURRENT_SOURCE_DIR}/stub_cryptor.hpp")
    endif()
    add_test(NAME ${test} COMMAND ${test})
    set_tests_properties(${test} PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" TIMEOUT 120)
endforeach()

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/wpp")
set_tests_properties(test_wpp PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/wpp")
add_test(NAME test_touch_split COMMAND test_touch)
set_tests_properties(test_touch test_touch_split PROPERTIES
    ENVIRONMENT "NBT_TOUCH_CONNECT=0" RESOURCE_LOCK touch_port)
set_tests_properties(test_touch_split PROPERTIES
    ENVIRONMENT "NBT_TOUCH_CONNECT=0;TOUCH_SPLIT=1"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" TIMEOUT 120)

option(NBT_HOST_TSAN "Run the lifecycle test with ThreadSanitizer when supported" ON)
if(NBT_HOST_TSAN)
    include(CheckCXXSourceCompiles)
    include(CMakePushCheckState)
    cmake_push_check_state(RESET)
    set(CMAKE_REQUIRED_FLAGS "-fsanitize=thread")
    set(CMAKE_REQUIRED_LIBRARIES "-fsanitize=thread")
    check_cxx_source_compiles("int main() { return 0; }" NBT_HAS_TSAN)
    cmake_pop_check_state()
    if(NBT_HAS_TSAN)
        add_executable(test_session_lifecycle_tsan "${CMAKE_CURRENT_SOURCE_DIR}/test_session_lifecycle.cpp")
        target_sources(test_session_lifecycle_tsan PRIVATE "${PROJECT_SOURCE_DIR}/src/aa_session.cpp")
        target_link_libraries(test_session_lifecycle_tsan PRIVATE host_support)
        target_compile_options(test_session_lifecycle_tsan PRIVATE -fsanitize=thread -O1 -g
            -include "${CMAKE_CURRENT_SOURCE_DIR}/stub_cryptor.hpp")
        target_link_options(test_session_lifecycle_tsan PRIVATE -fsanitize=thread)
        add_test(NAME test_session_lifecycle_tsan COMMAND test_session_lifecycle_tsan)
        set_tests_properties(test_session_lifecycle_tsan PROPERTIES
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" TIMEOUT 120
            ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1" LABELS tsan)
        message(STATUS "ThreadSanitizer lifecycle check enabled")
    else()
        message(STATUS "ThreadSanitizer lifecycle check unavailable: compiler/linker probe failed")
    endif()
else()
    message(STATUS "ThreadSanitizer lifecycle check explicitly disabled")
endif()

# Compile only: the fake-framework harness predates the current BMW plugin ABI.
add_library(rhmiapp_host SHARED "${PROJECT_SOURCE_DIR}/rhmiapp/nbt_aad_app.cpp")
target_link_libraries(rhmiapp_host PRIVATE host_test_config)
set_target_properties(rhmiapp_host PROPERTIES OUTPUT_NAME NbtAad)

file(GLOB python_tests CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test_*.py")
foreach(source IN LISTS python_tests)
    get_filename_component(test "${source}" NAME_WE)
    add_test(NAME ${test} COMMAND "${Python3_EXECUTABLE}" "${source}")
    set_tests_properties(${test} PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" TIMEOUT 120)
endforeach()
