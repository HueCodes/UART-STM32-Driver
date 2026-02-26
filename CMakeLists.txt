cmake_minimum_required(VERSION 3.16)
project(uart_cpp LANGUAGES CXX ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Firmware build (cross-compiled for Cortex-M4)
# ---------------------------------------------------------------------------

if(CMAKE_CROSSCOMPILING)
    # MCU-specific flags for Cortex-M4 with FPU (STM32F4xx)
    set(MCU_FLAGS "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard")

    set(CMAKE_CXX_FLAGS "${MCU_FLAGS} -Wall -Wextra -Wpedantic -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections -Os")
    set(CMAKE_C_FLAGS   "${MCU_FLAGS} -Wall -Wextra -ffunction-sections -fdata-sections -Os")
    set(CMAKE_EXE_LINKER_FLAGS "${MCU_FLAGS} -Wl,--gc-sections -specs=nano.specs -specs=nosys.specs")

    # CMSIS headers: place STM32CubeF4 (or standalone CMSIS) at cmsis/
    #   cmsis/Include/                       -- CMSIS core headers (core_cm4.h etc.)
    #   cmsis/Device/ST/STM32F4xx/Include/   -- STM32F4 device headers (stm32f4xx.h etc.)
    include_directories(
        include
        cmsis/Include
        cmsis/Device/ST/STM32F4xx/Include
    )

    # Select the exact device variant -- adjust if using F411RE or F446RE.
    add_definitions(-DSTM32F401xE)

    add_executable(uart_demo
        src/startup.cpp
        src/main.cpp
        src/uart.cpp
        src/rcc.cpp
        src/syscalls.cpp
    )

    target_link_options(uart_demo PRIVATE
        -T${CMAKE_SOURCE_DIR}/linker/stm32f401xe.ld
        -Wl,-Map=${CMAKE_BINARY_DIR}/uart_demo.map
    )

    # Post-build: generate .hex and .bin alongside the ELF, print section sizes.
    add_custom_command(TARGET uart_demo POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:uart_demo> ${CMAKE_BINARY_DIR}/uart_demo.hex
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:uart_demo> ${CMAKE_BINARY_DIR}/uart_demo.bin
        COMMAND ${CMAKE_SIZE}              $<TARGET_FILE:uart_demo>
        COMMENT "Generating HEX and BIN"
    )

    # Flash via OpenOCD + ST-Link.
    add_custom_target(flash
        COMMAND openocd -f interface/stlink.cfg -f target/stm32f4x.cfg
                -c "program ${CMAKE_BINARY_DIR}/uart_demo.hex verify reset exit"
        DEPENDS uart_demo
        COMMENT "Flashing uart_demo.hex via ST-Link"
    )

else()
    # -----------------------------------------------------------------------
    # Host build: unit tests only (no cross-compiler required)
    # Run with:
    #   cmake -B build_host
    #   cmake --build build_host
    #   ctest --test-dir build_host/tests --output-on-failure
    # -----------------------------------------------------------------------
    enable_testing()
    add_subdirectory(tests)
endif()
