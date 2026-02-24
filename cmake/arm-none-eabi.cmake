set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Prefer the official Arm GNU Toolchain (includes newlib) if installed.
# Falls back to whatever arm-none-eabi-g++ is on PATH.
if(EXISTS /opt/homebrew/opt/arm-gnu-toolchain/bin/arm-none-eabi-g++)
    set(TC /opt/homebrew/opt/arm-gnu-toolchain/bin)
else()
    set(TC "")
endif()

set(CMAKE_C_COMPILER   ${TC}/arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER ${TC}/arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER ${TC}/arm-none-eabi-gcc)
set(CMAKE_OBJCOPY      ${TC}/arm-none-eabi-objcopy)
set(CMAKE_SIZE         ${TC}/arm-none-eabi-size)

# Prevent CMake from testing the compiler with a full executable
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
