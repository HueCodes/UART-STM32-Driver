# STM32F4 Bare-Metal UART Driver - Technical Reference

---

## Table of Contents

1. [Hardware Overview](#1-hardware-overview)
2. [Project Structure](#2-project-structure)
3. [Toolchain and Build System](#3-toolchain-and-build-system)
4. [Startup and Boot Sequence](#4-startup-and-boot-sequence)
5. [Linker Script](#5-linker-script)
6. [Clock Configuration (RCC)](#6-clock-configuration-rcc)
7. [GPIO Configuration](#7-gpio-configuration)
8. [USART2 Peripheral](#8-usart2-peripheral)
9. [Ring Buffer](#9-ring-buffer)
10. [Interrupt-Driven UART](#10-interrupt-driven-uart)
11. [NVIC and Interrupt Management](#11-nvic-and-interrupt-management)
12. [Error Flag Handling](#12-error-flag-handling)
13. [Syscalls and printf Routing](#13-syscalls-and-printf-routing)
14. [Memory Map and Sections](#14-memory-map-and-sections)
15. [C++ on Bare Metal](#15-c-on-bare-metal)
16. [Key Design Decisions](#16-key-design-decisions)
17. [Common Interview Questions](#17-common-interview-questions)

---

## 1. Hardware Overview

**Target:** STM32F401xE (Nucleo-F401RE board)

| Property | Value |
|---|---|
| Core | ARM Cortex-M4F (with FPU) |
| Max clock | 84 MHz |
| Flash | 512 KB at 0x08000000 |
| RAM | 96 KB at 0x20000000 |
| UART peripheral | USART2 |
| TX pin | PA2, Alternate Function 7 |
| RX pin | PA3, Alternate Function 7 |
| Virtual COM port | ST-Link USB bridge on the Nucleo |

The Nucleo board's ST-Link debugger exposes USART2 as a virtual COM port over USB, which is why PA2/PA3 are the fixed pins.

---

## 2. Project Structure

```
UART-cpp/
  include/
    rcc.hpp          -- RCC clock init declaration
    uart.hpp         -- Uart class, RingBuffer include, uart_instance()
    ring_buffer.hpp  -- Lock-free SPSC ring buffer template
  src/
    startup.cpp      -- Vector table, Reset_Handler
    rcc.cpp          -- PLL clock setup to 84 MHz
    uart.cpp         -- UART driver, ISR, singleton
    syscalls.cpp     -- _write() stub for printf
    main.cpp         -- Echo demo
  linker/
    stm32f401xe.ld   -- Memory map and section layout
  cmake/
    arm-none-eabi.cmake -- Cross-compilation toolchain file
  CMakeLists.txt
  .clang-format
```

---

## 3. Toolchain and Build System

### Cross-Compilation

The ARM embedded toolchain (`arm-none-eabi-g++`) targets a bare-metal ARM processor. Key flags:

| Flag | Purpose |
|---|---|
| `-mcpu=cortex-m4` | Select Cortex-M4 instruction set |
| `-mthumb` | Use Thumb-2 (compact 16/32-bit mixed encoding) |
| `-mfpu=fpv4-sp-d16` | Enable the hardware FPU (single-precision, 16 registers) |
| `-mfloat-abi=hard` | Pass float arguments in FPU registers |
| `-fno-exceptions` | No C++ exception support (saves code size) |
| `-fno-rtti` | No runtime type info (saves code size) |
| `-ffunction-sections -fdata-sections` | Place each function/variable in its own section |
| `-Wl,--gc-sections` | Linker removes unused sections (dead code elimination) |
| `-specs=nano.specs` | Use newlib-nano (smaller C library) |
| `-specs=nosys.specs` | Provide stub syscalls; we override `_write` |
| `-Os` | Optimize for size |

### Why `-specs=nano.specs`?

Newlib-nano is a reduced version of the newlib C library, designed for microcontrollers. It has a smaller printf that lacks some features (e.g., float formatting by default). For an MCU with 96 KB RAM, code size matters.

### CMake Toolchain File

`cmake/arm-none-eabi.cmake` sets `CMAKE_SYSTEM_NAME Generic` (no OS) and `CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY` to prevent CMake from trying to link a test executable (which would fail without a startup file).

---

## 4. Startup and Boot Sequence

### What Happens at Power-On

1. The ARM core reads the initial stack pointer from address `0x08000000` (first word of Flash).
2. It then reads the reset vector from `0x08000000 + 4` and jumps to `Reset_Handler`.
3. `Reset_Handler` runs before `main()`.

### Reset_Handler Steps

```
Reset_Handler
  |
  +-- Copy .data section from Flash to RAM
  |     (initialized global/static variables)
  |
  +-- Zero .bss section in RAM
  |     (uninitialized global/static variables)
  |
  +-- __libc_init_array()
  |     (runs C++ global constructors via .init_array)
  |
  +-- main()
  |
  +-- spin forever (main should never return)
```

### Why Copy .data?

Global variables with non-zero initial values (e.g., `int x = 5`) have their initial values stored in Flash (at the load address) but must live in RAM at runtime. The linker marks the Flash copy address as `_sidata` and the RAM destination as `_sdata`/`_edata`. Reset_Handler copies word-by-word.

### Why Zero .bss?

The C standard guarantees that all global variables are zero-initialized if not explicitly initialized. They don't need Flash storage (no non-zero initial value), so the linker places them in `.bss`. Reset_Handler writes zeros to `_sbss` through `_ebss`.

### Vector Table

The vector table is a C array placed in `.isr_vector` at the very start of Flash:
- Entry 0: initial stack pointer value (not a function pointer - it is data)
- Entry 1: address of Reset_Handler
- Entries 2-15: ARM Cortex-M4 exception handlers
- Entries 16-97: STM32F401xE device-specific IRQ handlers (82 total)

USART2 is IRQ38, so it lives at vector table index 38+16 = 54.

The `__attribute__((section(".isr_vector"), used))` on the array forces the linker to keep it and place it in the correct section, even though nothing explicitly calls the array.

### Weak Aliases

All handlers except Reset_Handler and Default_Handler are declared as weak aliases pointing to Default_Handler:

```cpp
#define WEAK_ALIAS(name) \
    void name() __attribute__((weak, alias("Default_Handler")))
```

"Weak" means the linker will use a differently-named strong symbol if one exists in any object file. When uart.cpp defines `extern "C" void USART2_IRQHandler()` (a strong symbol), the linker replaces the weak alias in the vector table entry. All other IRQs fall through to Default_Handler, which spins forever for debugging.

---

## 5. Linker Script

### Memory Regions

```
FLASH (rx):  0x08000000, 512 KB  -- read/execute only
RAM   (rwx): 0x20000000, 96 KB   -- read/write/execute
```

### Section Layout (Flash)

```
Flash:
  .isr_vector  -- vector table (must be at start of Flash)
  .text        -- compiled code
                  .text*       -- all .text subsections
                  .rodata*     -- read-only data (string literals, const)
                  .init_array  -- C++ constructor function pointers
                  .fini_array  -- C++ destructor function pointers
```

### Section Layout (RAM)

```
RAM:
  .data  -- initialized globals (copied from Flash by Reset_Handler)
  .bss   -- zero-initialized globals
  [stack grows downward from top of RAM]
```

### .init_array and .fini_array

These sections hold tables of function pointers that `__libc_init_array()` / `__libc_fini_array()` iterate over to call global constructors and destructors. Without them in the linker script, the linker would either discard them or fail to find them.

The `KEEP()` directive prevents `--gc-sections` from discarding these tables even if no direct call to them is visible.

### `_estack`

Defined as `ORIGIN(RAM) + LENGTH(RAM)` = `0x20000000 + 0x18000` = `0x20018000`. This is the first address past the end of RAM, which is the initial value of the stack pointer (the stack grows downward on ARM).

---

## 6. Clock Configuration (RCC)

### STM32F401 Clock Tree

```
HSI (16 MHz internal RC oscillator) -- default after reset
HSE (8 MHz crystal on Nucleo)       -- more accurate
  |
  +-> PLL -> SYSCLK
               |
               +-> AHB prescaler -> HCLK (CPU + AHB peripherals)
                     |
                     +-> APB1 prescaler -> PCLK1 (low-speed peripherals, max 42 MHz)
                     +-> APB2 prescaler -> PCLK2 (high-speed peripherals, max 84 MHz)
```

### PLL Configuration for 84 MHz

The PLL equation:
```
VCO_in  = HSE / M         = 8 MHz / 8 = 1 MHz
VCO_out = VCO_in * N      = 1 MHz * 336 = 336 MHz
SYSCLK  = VCO_out / P     = 336 MHz / 4 = 84 MHz
USB_CLK = VCO_out / Q     = 336 MHz / 7 = 48 MHz (USB spec requires 48 MHz)
```

VCO input must be 1-2 MHz (datasheet constraint). VCO output must be 64-432 MHz. Both are satisfied.

### Why M=8?

HSE is 8 MHz. M=8 gives VCO_in = 1 MHz, which is in the allowed range. This is the recommended value for an 8 MHz crystal.

### PLLP Encoding

The PLLP field in RCC_PLLCFGR is 2 bits encoding the divide value:
- 00 = /2
- 01 = /4
- 10 = /6
- 11 = /8

So P=4 is written as `1` (binary `01`) to the PLLP field.

### Flash Latency

At 84 MHz and 3.3 V, the ARM core can read Flash at most once per clock cycle. Flash is slower than 84 MHz, so wait states must be inserted:

| SYSCLK range | Wait states |
|---|---|
| 0-30 MHz | 0 WS |
| 30-64 MHz | 1 WS |
| 64-84 MHz | 2 WS |

**Critical:** flash latency must be increased before increasing SYSCLK. If you switch to 84 MHz with 0 wait states, the core will read garbage from Flash and crash.

### Bus Prescalers

USART2 hangs off APB1. With APB1 prescaler = /2:
- HCLK = 84 MHz
- PCLK1 = 84 / 2 = 42 MHz (APB1 max is 42 MHz on F401)

The baud rate calculation in `init()` reads the PPRE1 bits from RCC_CFGR at runtime rather than hardcoding 42 MHz. This makes the code correct regardless of what prescaler is actually set.

---

## 7. GPIO Configuration

### STM32F4 GPIO Registers

Each GPIO pin is controlled by several registers. For PA2 (TX) and PA3 (RX):

**MODER** (Mode Register) - 2 bits per pin:
- `00` = input
- `01` = output
- `10` = alternate function
- `11` = analog

PA2 and PA3 are set to `10` (alternate function).

**OTYPER** (Output Type Register) - 1 bit per pin:
- `0` = push-pull
- `1` = open-drain

Push-pull: the driver actively drives both high and low. UART TX is push-pull.

**OSPEEDR** (Speed Register) - 2 bits per pin:
- `00` = low speed
- `01` = medium speed
- `10` = high speed
- `11` = very high speed

Set to high speed for reliable UART at 115200 baud.

**PUPDR** (Pull-Up/Pull-Down Register) - 2 bits per pin:
- `00` = no pull
- `01` = pull-up
- `10` = pull-down

No pull; UART lines are actively driven.

**AFR** (Alternate Function Register) - 4 bits per pin:
- AF7 (`0x7`) selects USART2 for PA2 and PA3
- AFR[0] covers pins 0-7 (4 bits each = 32 bits total)
- PA2 uses bits 11:8, PA3 uses bits 15:12

### Register Bit Manipulation Pattern

The code uses read-modify-write for most registers:
```cpp
// Clear the target bits first (AND with inverted mask)
GPIOA->MODER &= ~(0x3u << (2 * pin));
// Then set the desired value (OR in the value)
GPIOA->MODER |=  (0x2u << (2 * pin));
```

This is necessary for registers shared across pins. A plain assignment would reset other pins.

---

## 8. USART2 Peripheral

### Key Registers

**CR1** (Control Register 1):
- `UE` (bit 13): USART enable
- `M` (bit 12): word length (0=8 bits, 1=9 bits)
- `PCE` (bit 10): parity control enable
- `PS` (bit 9): parity selection (0=even, 1=odd)
- `RXNEIE` (bit 5): RXNE interrupt enable
- `TXEIE` (bit 7): TXE interrupt enable
- `TE` (bit 3): transmitter enable
- `RE` (bit 2): receiver enable

**CR2** (Control Register 2):
- `STOP` (bits 13:12): stop bits (00=1, 10=2)

**BRR** (Baud Rate Register):
- `BRR = PCLK1 / baud_rate` (for 16x oversampling, which is the default)
- Rounded to nearest: `BRR = (PCLK1 + baud/2) / baud`

**SR** (Status Register):
- `TXE` (bit 7): transmit data register empty (ready to accept next byte)
- `RXNE` (bit 5): receive data register not empty (a byte has arrived)
- `ORE` (bit 3): overrun error
- `NE` (bit 2): noise error
- `FE` (bit 1): framing error

**DR** (Data Register):
- Write a byte here to transmit
- Read a byte from here on receive
- Reading DR clears RXNE; writing DR clears TXE

### Baud Rate Calculation

USART uses 16x oversampling by default (OVER8=0). The BRR register holds a fixed-point number where the fractional part is in the lower 4 bits.

For integer baud rates, it simplifies to:
```
BRR = PCLK1 / baud
```

Rounding to nearest (to minimize error):
```
BRR = (PCLK1 + baud/2) / baud
```

Example: PCLK1=42 MHz, baud=115200
```
BRR = (42000000 + 57600) / 115200 = 365.1 -> 365
actual baud = 42000000 / 365 = 115068  (0.11% error, well within spec)
```

---

## 9. Ring Buffer

### Design Goals

- No heap (static storage, templated)
- Power-of-2 size for mask-based wrapping (no modulo = no division instruction)
- Lock-free single-producer single-consumer (SPSC)
- Safe to use from ISR and non-ISR simultaneously

### Implementation

```cpp
template <typename T, size_t N>
class RingBuffer {
    static_assert((N & (N - 1)) == 0);   // power-of-2 check
    static constexpr size_t MASK = N - 1;
    volatile size_t head_{0};            // write index
    volatile size_t tail_{0};            // read index
    T buf_[N];
};
```

**head**: points to the next slot to write. After writing, head advances.
**tail**: points to the next slot to read. After reading, tail advances.
**empty**: `head == tail`
**full**: `(head + 1) & MASK == tail` (one slot deliberately wasted to distinguish full from empty)

### Why volatile?

`volatile` on `head_` and `tail_` prevents the compiler from caching them in registers across function calls. Without volatile:
- The ISR might update `head_` after a byte arrives
- The non-ISR spin in `receive()` might not see the update because the compiler cached `head_` in a register

On a single-core processor like Cortex-M4, this is sufficient. There is no cache coherency problem because there is one cache domain.

### Why buf_ is not volatile

`buf_` is not volatile because:
- The producer writes `buf_[h]` and then updates `head_` (volatile)
- The consumer reads `head_` (volatile) to discover new data, then reads `buf_[t]`
- The volatile read of `head_` acts as a compiler barrier: the compiler cannot reorder the `buf_` read before the `head_` read
- On ARM Cortex-M4, there is no hardware reordering of accesses to normal memory (strongly-ordered or device memory)

Making `buf_` volatile would add unnecessary load/store instructions on every element access.

### Capacity

With `N=256`, the buffer holds up to 255 bytes (one slot wasted). This is sufficient for a UART echo application. For higher throughput, increase N (must remain a power of 2).

---

## 10. Interrupt-Driven UART

### TX Path (Non-Blocking Send)

```
Application calls send(byte)
  |
  +-- push byte into tx_buf_ (ring buffer)
  |
  +-- USART2->CR1 |= TXEIE  (enable TXE interrupt)
  |
  return (non-blocking)

TXE interrupt fires (transmitter idle):
  |
  +-- pop byte from tx_buf_
  |     success: write byte to DR, return
  |     empty:   disable TXEIE, return
```

The TXE (Transmit data register Empty) flag is set when DR can accept a new byte. The interrupt fires only when both `TXEIE=1` and `TXE=1`. Once the buffer is drained, the ISR disables TXEIE to prevent continuous spurious interrupts.

### RX Path (Interrupt-Driven, Blocking Receive)

```
RXNE interrupt fires (a byte arrived in DR):
  |
  +-- read DR -> push to rx_buf_
  |
  return

Application calls receive()
  |
  +-- spin: while rx_buf_.pop(byte) fails {}
  |
  return byte
```

RXNEIE stays permanently enabled after `init()`. Every received byte is pushed to the ring buffer by the ISR. The application spins until the buffer has data.

### Why This Design

- **Non-blocking TX**: The application is not stalled waiting for the slow UART transmitter. The CPU can do other work (or go to sleep) while the ISR feeds bytes to the hardware.
- **Interrupt RX**: Bytes are buffered automatically; they won't be missed while the CPU is doing other work. Without interrupts, the CPU must poll RXNE frequently or bytes are dropped.

### Race Condition Analysis

TX path: `send()` pushes to the buffer then enables the interrupt. The ISR is the sole consumer of tx_buf_. This is SPSC (single producer, single consumer) - safe.

RX path: The ISR pushes to rx_buf_. `receive()` is the sole consumer. Also SPSC - safe.

The `volatile` head/tail ensure the compiler does not hoist reads across the push/pop boundary.

---

## 11. NVIC and Interrupt Management

### NVIC Overview

The Nested Vectored Interrupt Controller (NVIC) is the ARM-standard interrupt controller on all Cortex-M devices. It:
- Enables/disables specific IRQs
- Assigns priorities (0 = highest, 255 = lowest on some devices)
- Handles preemption and tail-chaining

### Setup in init()

```cpp
NVIC_SetPriority(USART2_IRQn, 5);  // priority 5 (lower number = higher priority)
NVIC_EnableIRQ(USART2_IRQn);       // enable the IRQ
```

`USART2_IRQn` is defined as `38` in the CMSIS device header. NVIC functions are inlined in `core_cm4.h` and manipulate the NVIC registers directly.

### Priority Groups

The Cortex-M4 uses 4 bits for priority (16 levels, 0-15 after shifting). Priority 5 is a mid-range value. It will be preempted by hardware exceptions (HardFault, NMI) and any IRQ with priority 0-4, but will preempt any IRQ with priority 6-15.

### How the Vector Table Works at Runtime

When USART2 raises an interrupt:
1. The CPU finishes the current instruction (or an exceptional point during a load/store multiple)
2. The CPU pushes PC, PSR, LR, R0-R3, R12 onto the stack (hardware context save)
3. The NVIC reads the vector table entry at `0x08000000 + 54*4 = 0x080000D8`
4. This contains the address of `USART2_IRQHandler`
5. The CPU jumps to it
6. On return via `BX LR` (with special EXC_RETURN value), hardware pops the saved registers

### extern "C" Requirement

The ISR must be declared `extern "C"` to prevent C++ name mangling. The vector table contains the raw address of the function. If the function name is mangled (e.g., `_ZN4Uart19USART2_IRQHandlerEv`), it won't match the unmangled `USART2_IRQHandler` symbol that the weak alias in the vector table refers to.

---

## 12. Error Flag Handling

### STM32F4 USART Error Flags

| Flag | Bit | Meaning |
|---|---|---|
| ORE | SR[3] | Overrun: new byte arrived before DR was read; the new byte is lost |
| NE | SR[2] | Noise: signal did not meet timing thresholds |
| FE | SR[1] | Framing: stop bit was not detected at expected time |

### Clearing Error Flags

STM32F4 uses a two-step clear sequence:
1. Read SR (to capture which flags are set)
2. Read DR (to clear the flags)

Reading DR is the mandatory second step. This is different from STM32F7/H7 which use explicit clear-by-write bits.

**Implication:** reading DR to clear an error also clears RXNE, consuming the byte. The byte associated with FE or NE may be delivered (with corrupt data). The byte when ORE is set is the stale pre-overrun byte; the new overflowed byte is lost.

### Why Clear Before Checking RXNE

If ORE or FE is set, RXNE is also set (there is data in DR). If we read RXNE and push the byte to the ring buffer without clearing the error flags, the RXNE interrupt will fire again immediately (ORE keeps RXNE asserted). This stalls the receiver in an infinite interrupt loop.

By calling `get_and_clear_errors()` first, which reads DR if errors exist, RXNE is cleared. The second SR read in `irq_handler()` sees RXNE=0 and correctly skips the push.

---

## 13. Syscalls and printf Routing

### newlib Syscalls

newlib (the C library used with arm-none-eabi) calls a set of platform-provided functions for I/O:
- `_write(int fd, const char* buf, int len)` - output bytes
- `_read(int fd, char* buf, int len)` - input bytes
- `_sbrk(ptrdiff_t incr)` - heap expansion (we avoid the heap entirely)

With `-specs=nosys.specs`, these are provided as stubs that return `-1` (error). Our `syscalls.cpp` overrides `_write`:

```cpp
extern "C" int _write(int fd, const char* buf, int len)
{
    if (fd == 1 || fd == 2) {  // stdout or stderr
        uart_instance().send(buf, static_cast<size_t>(len));
    }
    return len;
}
```

Now `printf("hello %d\n", 42)` will format into a buffer and call `_write`, which routes bytes through the UART ring buffer.

### Why the Singleton

`syscalls.cpp` cannot include a `Uart` object (it does not own one). It must reach the same `Uart` instance that `main.cpp` initialized. The singleton pattern (`uart_instance()` returning `s_uart` from uart.cpp) solves this: any translation unit that includes `uart.hpp` can call `uart_instance()` and get the same object.

---

## 14. Memory Map and Sections

### Static vs. Dynamic Allocation

This project uses no heap (`new`, `delete`, `malloc`, `free`). All objects are:
- Stack-allocated (local variables, function call frames)
- Statically allocated (global/static variables in `.data` or `.bss`)

Benefits:
- Predictable memory usage at compile/link time
- No fragmentation
- No `_sbrk` needed (the heap expansion syscall)

### Ring Buffer Memory

`RingBuffer<uint8_t, 256>` uses `256 + 2*sizeof(size_t)` bytes = ~264 bytes. Two ring buffers = ~528 bytes of RAM, well within 96 KB.

They are static class members (`static RingBuffer<uint8_t, 256> tx_buf_, rx_buf_`), so they have static storage duration. They go in `.bss` (zero-initialized, which matches the zero-head/zero-tail initial state).

### Stack Location

The stack pointer starts at `_estack = 0x20018000` (top of RAM) and grows downward. The linker does not enforce a stack size limit; a stack overflow will silently corrupt `.bss` data. For production code, a stack canary or MPU guard region would be added.

---

## 15. C++ on Bare Metal

### What is Disabled

| Feature | Flag | Why |
|---|---|---|
| Exceptions | `-fno-exceptions` | require runtime support tables, large code/data overhead |
| RTTI | `-fno-rtti` | `typeid`, `dynamic_cast` require type metadata tables |
| Heap | (convention) | unpredictable latency, fragmentation, no OS to reclaim |

### What Works Fine

- Classes, methods, constructors/destructors
- Templates (entirely compile-time)
- `constexpr`, `static_assert`
- `std::string_view` (zero-cost wrapper over pointer+length, no heap)
- `enum class`
- Initializer lists

### Global Constructor Execution

Calling `__libc_init_array()` in `Reset_Handler` runs all constructors for objects with static storage duration. These constructors run before `main()`. For our `s_uart` object, the constructor just stores the baud/parity/stop values. For the ring buffers, the default constructor sets head=tail=0.

The original startup commented out `__libc_init_array()` to keep things simple. We uncommented it because the singleton (`s_uart`) and any future non-trivial global objects need it.

### `std::string_view`

`string_view` is a C++17 non-owning view of a contiguous sequence of characters. It is essentially:
```cpp
struct string_view {
    const char* data;
    size_t size;
};
```
No allocation, no copying. `send(std::string_view sv)` is more type-safe than `send(const char* str, size_t len)` because the length is bundled with the pointer.

---

## 16. Key Design Decisions

### Why No ST HAL?

The ST HAL (Hardware Abstraction Layer) is a large, general-purpose library:
- ~20,000 lines of C for just the UART
- Runtime configurability for every possible use case
- Callback mechanism with overhead
- Adds `volatile` incorrectly in some places

For a dedicated driver, direct register access is smaller, faster, and completely understandable from the reference manual alone.

### Why CMSIS?

CMSIS (Cortex Microcontroller Software Interface Standard) provides:
- Peripheral register definitions (e.g., `USART2->SR`)
- Bit field macros (e.g., `USART_SR_RXNE`)
- Core functions (e.g., `NVIC_EnableIRQ`)

These are thin definitions derived directly from the datasheet. Using them means the code is readable and auditable against the reference manual without introducing any behavior of its own.

### Why Static Ring Buffer Members?

`irq_handler()` is a static method (no `this` pointer). It needs to access the ring buffers. Making them static members of `Uart` allows `irq_handler()` to reach them as `Uart::tx_buf_` and `Uart::rx_buf_` without a singleton pointer or a global.

This is appropriate because there is exactly one USART2 peripheral on the chip.

### Why Non-Blocking TX?

Blocking TX (`while (!(SR & TXE)) {}`) is simple but wasteful: at 115200 baud, each byte takes ~87 microseconds. The CPU spins with interrupts enabled, doing nothing useful. Non-blocking TX frees the CPU immediately; the ISR handles the slow hardware clock.

### TX Ring Buffer Full

If `tx_buf_.push()` returns false (buffer full), the byte is silently dropped. For a simple echo demo this is acceptable. In a production system you would either:
- Block until space is available (but not in an ISR context)
- Return an error code from `send()`
- Use flow control (hardware RTS/CTS or software XON/XOFF)

---

## 17. Common Interview Questions

**Q: What is the difference between TXE and TC (Transmission Complete) in USART?**

TXE (Transmit data register Empty) goes high when the DR is ready to accept a new byte, even while the shift register is still sending the previous byte. TC (Transmission Complete) goes high only after the last bit of the last byte has left the shift register. TXE is used for back-to-back byte transmission. TC is used when you need to know that all data has physically left the chip (e.g., before disabling the transmitter or asserting a direction line for RS-485).

**Q: Why must you set flash wait states before switching to a higher clock?**

Flash access time is fixed. If you switch SYSCLK to 84 MHz while flash is configured for 0 wait states, the CPU will request data faster than flash can deliver it. The flash controller will return stale or indeterminate data, causing the CPU to execute garbage instructions. Always increase latency first, then increase clock.

**Q: What is the ARM stack convention and which direction does it grow?**

ARM (and Thumb) use a full-descending stack: the stack pointer points to the last used location, and the stack grows downward (toward lower addresses). On push, SP is decremented first, then data is stored. On pop, data is loaded first, then SP is incremented.

**Q: Why does USART2_IRQHandler need extern "C"?**

C++ mangles function names to encode argument types (e.g., for overloading). The vector table contains the raw symbol address looked up by the linker as a plain C name. If the function is compiled with C++ name mangling, the linker cannot match it to the `USART2_IRQHandler` weak alias in the vector table. `extern "C"` disables mangling for that function.

**Q: What is the SPSC (single-producer single-consumer) property and why does it matter here?**

SPSC means exactly one context writes to the queue (the producer) and exactly one context reads from it (the consumer). This is a key invariant for lock-free ring buffers: only one thread/context advances `head_` and only one advances `tail_`. If multiple producers or consumers existed, they could race to update the same index.

For TX: `send()` (non-ISR context) is the sole producer; `irq_handler()` (ISR) is the sole consumer.
For RX: `irq_handler()` (ISR) is the sole producer; `receive()` (non-ISR) is the sole consumer.

**Q: How does __libc_init_array work?**

`__libc_init_array` iterates over function pointers stored in the `.init_array` section. The linker script places this section in Flash. Each entry is a pointer to a constructor function generated by the compiler for a global/static object. The function is called before main(), constructing each object in an implementation-defined order (generally the link order).

**Q: What happens if the TX ring buffer fills up?**

`push()` returns false and the byte is dropped silently. In this implementation there is no back-pressure. At 115200 baud, the ISR sends bytes at ~11,520 bytes/second. If the application sends faster than that, the 255-byte buffer provides ~22 ms of burst capacity before dropping.

**Q: What is the purpose of the read-back after enabling a clock (volatile dummy read)?**

On Cortex-M4 with AHB/APB bus architecture, a write to a peripheral clock enable register may not be immediately visible to the peripheral. The write propagates through the bus matrix with a few cycle delay. Reading back the same register forces a synchronization point: the CPU stalls until the write reaches the bus, ensuring the peripheral is clocked before we access its registers.

**Q: Why is there a one-slot gap in the ring buffer (capacity = N-1, not N)?**

If head == tail means empty, then to represent a full buffer, we need a sentinel value that is distinct from empty. Using one wasted slot: full is `(head + 1) % N == tail`. This avoids needing a separate count or a flag, keeping the state to just two indices. For N=256, capacity is 255 bytes.

**Q: What does -mfloat-abi=hard mean and when would you use softfp?**

`hard`: float arguments are passed in FPU registers (S0-S15 for single-precision). The FPU hardware is used. Produces the most efficient code for float-heavy applications. Requires that all linked libraries also use `hard` ABI.

`softfp`: float arguments are passed in integer registers, but FPU instructions are used internally. A compromise: interoperates with soft-float libraries while still using the FPU.

`soft`: no FPU instructions; all float operations are implemented as software calls. Used when there is no FPU.

For STM32F401 which has an FPU, `hard` is correct.

---

## Build Instructions

Install the toolchain:
```
brew install arm-none-eabi-gcc
```

Configure and build:
```
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake
cmake --build build
```

Flash (requires OpenOCD and a connected Nucleo):
```
cmake --build build --target flash
```

Connect to the virtual COM port:
```
# macOS
screen /dev/tty.usbmodem* 115200

# Linux
screen /dev/ttyACM0 115200
```

Run host-side unit tests (no hardware or cross-compiler required):
```
cmake -B build_host
cmake --build build_host
ctest --test-dir build_host --output-on-failure
```

---

## Interview Topics

### 1. Lock-free SPSC ring buffer — why volatile is correct here

The ring buffer uses two indices: `head_` (written only by the producer) and
`tail_` (written only by the consumer). The `volatile` qualifier prevents the
compiler from caching these values in registers across function call boundaries
or interrupt returns.

On ARMv7-M (Cortex-M4):
- Naturally-aligned 32-bit reads and writes are single-cycle and cannot be
  torn by an interrupt. Therefore a single `head_` or `tail_` load/store is
  atomic without any explicit memory barrier.
- The SPSC invariant means only one side ever writes each index, so there is
  no write-write race.
- `volatile` is sufficient to prevent the compiler from holding a stale copy
  of `tail_` in a register across an `irq_handler()` call that updates it.

`std::atomic<size_t>` with `memory_order_relaxed` acquire/release would also
be correct and would express the intent more precisely to a reader familiar
with the C++ memory model. It was not used here to avoid pulling in `<atomic>`
and to keep the code readable for engineers who are more comfortable with the
hardware-level reasoning. In a team codebase, `std::atomic` would be preferred.

### 2. Why DMA is the next logical step — and what it would change

At 115200 baud each byte takes ~87 µs. An interrupt fires per byte. At
1 Mbaud (USB CDC virtual COM port speed) that is 10 µs per byte, which
consumes a significant fraction of CPU time on a 84 MHz core.

DMA eliminates the per-byte interrupt:
- TX: Load the DMA stream with a buffer pointer and length. The DMA controller
  copies from RAM to `USART->DR` autonomously. A single interrupt fires when
  the transfer is complete (or at half-transfer for double-buffering).
- RX: Configure a circular DMA buffer. The processor reads at its own pace by
  comparing its software read pointer against the DMA write pointer.

Changes required:
- Configure DMA1 streams (Stream 5 = USART2 TX, Stream 5 = USART2 RX on F401).
- Replace the TXE ISR with a DMA transfer-complete ISR.
- Replace the RXNE ISR with a periodic check of the DMA NDTR register.
- Remove `USART_CR1_TXEIE` and `USART_CR1_RXNEIE`; enable DMA requests instead
  (`USART_CR3_DMAT` and `USART_CR3_DMAR`).

The ring buffer would remain for the TX staging area; the DMA would drain it
in bursts rather than one byte at a time.

### 3. NVIC priority chosen and why — and what priority grouping means

Priority 5 is used for all UART interrupts in this driver. On STM32F4 the
NVIC implements 4 bits of priority (16 levels, 0 = highest). Priority 5 means:
- Lower priority than SysTick (default priority 0, which drives the 1 ms tick).
- Higher priority than any application-level interrupts at 6 or above.

Priority grouping (`NVIC_SetPriorityGrouping`) splits the 4 priority bits into
preemption priority (upper bits) and sub-priority (lower bits). This driver
does not call `NVIC_SetPriorityGrouping`, so the reset-default grouping (all
4 bits are preemption priority, no sub-priority) applies. This means a higher-
priority ISR can preempt a lower-priority ISR (tail-chaining / nesting).

If the application requires that UART ISRs be non-preemptible (e.g. because
they share state with another ISR at the same level), call
`NVIC_SetPriorityGrouping(4)` to use 0 preemption bits and 4 sub-priority bits.

### 4. BRR calculation — formula, rounding, and expected baud error

The USART baud rate register is:

    BRR = round(PCLK / baud)

The `uart_compute_brr()` function uses integer rounding:

    BRR = (PCLK + baud/2) / baud

The `+ baud/2` term rounds to nearest rather than truncating, eliminating the
systematic negative bias that truncation introduces.

Example — USART2 at 115200 baud, PCLK1 = 42 MHz:
    BRR     = (42000000 + 57600) / 115200 = 365
    Actual  = 42000000 / 365 = 115068 baud
    Error   = |115068 - 115200| / 115200 = 0.11%   (spec limit: ±2%)

The BRR register on STM32F4 USART peripherals contains both the integer part
(DIV_Mantissa, bits [15:4]) and the fractional part (DIV_Fraction, bits [3:0])
of the divisor. When oversampling by 16 (OVER8=0, the default), the fractional
part represents sixteenths. The formula above computes the full 12.4 fixed-point
value as an integer, which is exactly what the BRR register expects.

### 5. Why the startup .data copy must happen before __libc_init_array

The `.data` section holds initialised global and static variables. At reset,
only the load address (in Flash) is valid; the run-time address (in RAM) is
uninitialised. The startup code copies the initialised values from Flash to RAM
before `__libc_init_array()` is called.

`__libc_init_array()` runs constructors for global C++ objects (the `.init_array`
section) and newlib's own initialisation. These constructors may access global
variables — for example, a global `Uart` object's constructor reads the `baud_`
member that was set by an initialiser. If `.data` has not been copied yet, the
constructor would read uninitialised RAM and produce undefined behaviour.

Order: copy `.data` -> zero `.bss` -> call `__libc_init_array()` -> call `main()`.

### 6. Why C++ global constructors are safe here (no dynamic memory)

The `Uart` and `RingBuffer` constructors are safe to call from `__libc_init_array()`
because:
- They do not allocate heap memory (`new`, `malloc`).
- They do not call virtual functions whose vtable might not yet be set up.
- They only initialise POD member variables to known values.
- The `.bss` section has already been zeroed, so any zero-initialised members
  are in a consistent state before the constructor runs.

The absence of exceptions (`-fno-exceptions`) and RTTI (`-fno-rtti`) further
ensures that no hidden runtime support code is invoked during construction.

### 7. The ISR/main-thread data hazard — and how it is resolved

The hazard: `rx_buf_` is written by `irq_handler()` (ISR context) and read by
`receive()` (main-thread context). Similarly, `tx_buf_` is written by `send()`
(main) and read by `irq_handler()` (ISR).

Resolution: the SPSC ring buffer invariant (see topic 1) combined with `volatile`
indices ensures correctness without a mutex:
- `head_` is the write index. Only the producer writes it; the consumer reads it.
- `tail_` is the read index. Only the consumer writes it; the producer reads it.
- Because each index is written by exactly one context, there is no write-write
  race. The `volatile` qualifier ensures each access goes to memory, not a
  cached register value.

The `stats_` struct is written exclusively from ISR context (incrementing
counters) and read exclusively from main context (`get_stats()`). On ARMv7-M,
each 32-bit field read is atomic, so individual counter reads are not torn.
A snapshot of the full struct may be inconsistent if an interrupt fires between
two field reads; this is acceptable for diagnostic counters. If strict consistency
is required, disable interrupts around the copy in `get_stats()`.

### 8. Multi-UART instance dispatch

The driver supports USART1, USART2, and USART6 via a static dispatch table:
```
static Uart* s_usart1_inst;
static Uart* s_usart2_inst;
static Uart* s_usart6_inst;
```
Each `Uart::init()` call registers `this` in the appropriate slot. The
`extern "C"` ISR handlers index into this table and call the instance's
`irq_handler()`. Null-pointer guards prevent faults if an IRQ fires before
`init()` is called.

A natural extension would be to make the buffer sizes template parameters
(`UartDriver<TxSize, RxSize>`) with a virtual base class (`UartBase`) holding
the dispatch pointer. This allows different UART instances to have different
buffer sizes while sharing a single dispatch mechanism. The virtual call overhead
(one indirect branch per ISR) is negligible at UART data rates.
