// Minimal startup for STM32F401xE (84 MHz max, Cortex-M4).
//
// Provides:
//   - The vector table (first 16 ARM entries + NVIC entries we need)
//   - Reset_Handler: copies .data, zeroes .bss, calls main()
//
// This deliberately avoids __libc_init_array (C++ global constructors) to
// keep the demo minimal.  If you add global objects, uncomment that call.

#include <cstdint>
#include <cstring>

// Symbols defined by the linker script
extern uint32_t _sdata;   // .data destination start (RAM)
extern uint32_t _edata;   // .data destination end
extern uint32_t _sidata;  // .data source start (Flash)
extern uint32_t _sbss;    // .bss start
extern uint32_t _ebss;    // .bss end
extern uint32_t _estack;  // top of stack (set by linker to end of RAM)

extern int main();

// Forward declare handlers so the vector table can reference them
extern "C" {
    void Reset_Handler();
    void Default_Handler();
}

// Weak aliases: all unhandled interrupts go to Default_Handler
#define WEAK_ALIAS(name) \
    void name() __attribute__((weak, alias("Default_Handler")))

WEAK_ALIAS(NMI_Handler);
WEAK_ALIAS(HardFault_Handler);
WEAK_ALIAS(MemManage_Handler);
WEAK_ALIAS(BusFault_Handler);
WEAK_ALIAS(UsageFault_Handler);
WEAK_ALIAS(SVC_Handler);
WEAK_ALIAS(DebugMon_Handler);
WEAK_ALIAS(PendSV_Handler);
WEAK_ALIAS(SysTick_Handler);

// The vector table is placed in the .isr_vector section by the linker script.
// Each entry is a function pointer; the first two are the initial stack pointer
// and the reset handler address (ARM Cortex-M convention).
__attribute__((section(".isr_vector"), used))
static const void* vector_table[] = {
    /* 0: Initial stack pointer */    reinterpret_cast<void*>(&_estack),
    /* 1: Reset                  */   reinterpret_cast<void*>(Reset_Handler),
    /* 2: NMI                    */   reinterpret_cast<void*>(NMI_Handler),
    /* 3: HardFault              */   reinterpret_cast<void*>(HardFault_Handler),
    /* 4: MemManage              */   reinterpret_cast<void*>(MemManage_Handler),
    /* 5: BusFault               */   reinterpret_cast<void*>(BusFault_Handler),
    /* 6: UsageFault             */   reinterpret_cast<void*>(UsageFault_Handler),
    /* 7-10: Reserved            */   nullptr, nullptr, nullptr, nullptr,
    /* 11: SVCall                */   reinterpret_cast<void*>(SVC_Handler),
    /* 12: DebugMon              */   reinterpret_cast<void*>(DebugMon_Handler),
    /* 13: Reserved              */   nullptr,
    /* 14: PendSV                */   reinterpret_cast<void*>(PendSV_Handler),
    /* 15: SysTick               */   reinterpret_cast<void*>(SysTick_Handler),
    // IRQ 0-81 (STM32F401) - all default for this demo
    // Add specific IRQ handlers here if using interrupt-driven UART later
};

extern "C" void Reset_Handler()
{
    // Copy initialised data from Flash to RAM
    uint32_t* src = &_sidata;
    uint32_t* dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }

    // Zero the BSS segment
    dst = &_sbss;
    while (dst < &_ebss) {
        *dst++ = 0;
    }

    // Uncomment to run C++ global constructors:
    // extern void __libc_init_array();
    // __libc_init_array();

    main();

    // Should never reach here; spin if it does
    for (;;) {}
}

extern "C" void Default_Handler()
{
    // Unhandled interrupt - spin so a debugger can catch it
    for (;;) {}
}
