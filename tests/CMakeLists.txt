cmake_minimum_required(VERSION 3.16)

include(FetchContent)

# ---------------------------------------------------------------------------
# Catch2 — fetched automatically; no manual installation required.
# ---------------------------------------------------------------------------
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.3
)
FetchContent_MakeAvailable(Catch2)

# ---------------------------------------------------------------------------
# Compiler flags for the host build
# ---------------------------------------------------------------------------
set(HOST_FLAGS "-Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS "${HOST_FLAGS}")

# ---------------------------------------------------------------------------
# ring_buffer_tests
#
# Pure header test — no CMSIS dependency.
# ---------------------------------------------------------------------------
add_executable(ring_buffer_tests
    ring_buffer_tests.cpp
)
target_include_directories(ring_buffer_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(ring_buffer_tests PRIVATE Catch2::Catch2WithMain)

# ---------------------------------------------------------------------------
# uart_logic_tests
#
# Compiles uart.cpp against the mock CMSIS headers so that register-level
# logic (BRR calculation, CR1/CR2 configuration, stats counters, ISR paths)
# can be exercised on the host without hardware.
#
# The mock headers (tests/mock/stm32f4xx.h) shadow the real CMSIS headers by
# being listed first in the include path.
# ---------------------------------------------------------------------------
add_executable(uart_logic_tests
    uart_logic_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/uart.cpp
)
target_include_directories(uart_logic_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mock   # must come first to shadow real CMSIS
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_definitions(uart_logic_tests PRIVATE
    STM32F401xE
)
target_link_libraries(uart_logic_tests PRIVATE Catch2::Catch2WithMain)

# ---------------------------------------------------------------------------
# CTest integration
# ---------------------------------------------------------------------------
include(CTest)
# Catch2's own CMake module provides catch_discover_tests(); it is made
# available automatically via FetchContent_MakeAvailable(Catch2).
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

catch_discover_tests(ring_buffer_tests)
catch_discover_tests(uart_logic_tests)
