// Startup for STM32F401xE (Cortex-M4, 84 MHz max).
// Vector table: 16 ARM entries + 82 device IRQ slots (indices 16-97).
// USART2_IRQHandler is IRQ38, vector index 54.

#include <cstdint>

extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sidata;
extern uint32_t _sbss;
extern uint32_t _ebss;
extern uint32_t _estack;

extern int main();
extern "C" void __libc_init_array();

extern "C" {
    void Reset_Handler();
    void Default_Handler();
    void HardFault_Handler();

    // Defined as strong (non-weak) symbols in uart.cpp.
    // Forward declarations here so the vector table initialiser below can
    // take their address without a WEAK_ALIAS (which would make the linker
    // prefer the Default_Handler alias over the real definition).
    void USART1_IRQHandler();
    void USART2_IRQHandler();
    void USART6_IRQHandler();
}

// ---------------------------------------------------------------------------
// HardFault register capture
//
// When a HardFault fires, HardFault_Handler() captures the Cortex-M4 fault
// status registers here before spinning. Attach a debugger, halt, and inspect
// this struct to diagnose the fault:
//
//   CFSR  — Configurable Fault Status (MemManage/BusFault/UsageFault details)
//   HFSR  — HardFault Status (forced/vecttbl flags)
//   MMFAR — MemManage Fault Address (valid when CFSR.MMARVALID=1)
//   BFAR  — BusFault Address        (valid when CFSR.BFARVALID=1)
//   sp    — Stacked SP at fault entry
// ---------------------------------------------------------------------------

struct FaultRegisters {
    uint32_t cfsr;   ///< SCB->CFSR  0xE000ED28
    uint32_t hfsr;   ///< SCB->HFSR  0xE000ED2C
    uint32_t mmfar;  ///< SCB->MMFAR 0xE000ED34
    uint32_t bfar;   ///< SCB->BFAR  0xE000ED38
    uint32_t sp;     ///< SP value when the fault occurred
};

volatile FaultRegisters g_fault_regs{};

extern "C" void HardFault_Handler()
{
    // Read SCB fault status registers using their absolute MMIO addresses.
    // We avoid including core_cm4.h here to keep startup self-contained.
    g_fault_regs.cfsr  = *reinterpret_cast<volatile uint32_t*>(0xE000ED28u);
    g_fault_regs.hfsr  = *reinterpret_cast<volatile uint32_t*>(0xE000ED2Cu);
    g_fault_regs.mmfar = *reinterpret_cast<volatile uint32_t*>(0xE000ED34u);
    g_fault_regs.bfar  = *reinterpret_cast<volatile uint32_t*>(0xE000ED38u);

    // Capture the current stack pointer (MSP after CPU pushed the exception
    // frame, pointing at the bottom of the hardware-saved register set).
    __asm volatile ("mov %0, sp" : "=r"(g_fault_regs.sp));

    for (;;) {}  // halt — inspect g_fault_regs in debugger
}

#define WEAK_ALIAS(name) \
    void name() __attribute__((weak, alias("Default_Handler")))

// ARM Cortex-M4 system exception handlers
WEAK_ALIAS(NMI_Handler);
// HardFault_Handler: defined above as a strong (non-weak) symbol.
WEAK_ALIAS(MemManage_Handler);
WEAK_ALIAS(BusFault_Handler);
WEAK_ALIAS(UsageFault_Handler);
WEAK_ALIAS(SVC_Handler);
WEAK_ALIAS(DebugMon_Handler);
WEAK_ALIAS(PendSV_Handler);
WEAK_ALIAS(SysTick_Handler);

// STM32F401xE device IRQ handlers (IRQ0-IRQ81)
WEAK_ALIAS(WWDG_IRQHandler);
WEAK_ALIAS(PVD_IRQHandler);
WEAK_ALIAS(TAMP_STAMP_IRQHandler);
WEAK_ALIAS(RTC_WKUP_IRQHandler);
WEAK_ALIAS(FLASH_IRQHandler);
WEAK_ALIAS(RCC_IRQHandler);
WEAK_ALIAS(EXTI0_IRQHandler);
WEAK_ALIAS(EXTI1_IRQHandler);
WEAK_ALIAS(EXTI2_IRQHandler);
WEAK_ALIAS(EXTI3_IRQHandler);
WEAK_ALIAS(EXTI4_IRQHandler);
WEAK_ALIAS(DMA1_Stream0_IRQHandler);
WEAK_ALIAS(DMA1_Stream1_IRQHandler);
WEAK_ALIAS(DMA1_Stream2_IRQHandler);
WEAK_ALIAS(DMA1_Stream3_IRQHandler);
WEAK_ALIAS(DMA1_Stream4_IRQHandler);
WEAK_ALIAS(DMA1_Stream5_IRQHandler);
WEAK_ALIAS(DMA1_Stream6_IRQHandler);
WEAK_ALIAS(ADC_IRQHandler);
WEAK_ALIAS(EXTI9_5_IRQHandler);
WEAK_ALIAS(TIM1_BRK_TIM9_IRQHandler);
WEAK_ALIAS(TIM1_UP_TIM10_IRQHandler);
WEAK_ALIAS(TIM1_TRG_COM_TIM11_IRQHandler);
WEAK_ALIAS(TIM1_CC_IRQHandler);
WEAK_ALIAS(TIM2_IRQHandler);
WEAK_ALIAS(TIM3_IRQHandler);
WEAK_ALIAS(TIM4_IRQHandler);
WEAK_ALIAS(I2C1_EV_IRQHandler);
WEAK_ALIAS(I2C1_ER_IRQHandler);
WEAK_ALIAS(I2C2_EV_IRQHandler);
WEAK_ALIAS(I2C2_ER_IRQHandler);
WEAK_ALIAS(SPI1_IRQHandler);
WEAK_ALIAS(SPI2_IRQHandler);
// USART1_IRQHandler, USART2_IRQHandler, and USART6_IRQHandler are defined
// as real (non-weak) functions in uart.cpp. Declaring them as weak aliases
// here would cause the linker to silently discard the real definitions.
// They are therefore omitted from this list.
WEAK_ALIAS(EXTI15_10_IRQHandler);
WEAK_ALIAS(RTC_Alarm_IRQHandler);
WEAK_ALIAS(OTG_FS_WKUP_IRQHandler);
WEAK_ALIAS(DMA1_Stream7_IRQHandler);
WEAK_ALIAS(SDIO_IRQHandler);
WEAK_ALIAS(TIM5_IRQHandler);
WEAK_ALIAS(SPI3_IRQHandler);
WEAK_ALIAS(DMA2_Stream0_IRQHandler);
WEAK_ALIAS(DMA2_Stream1_IRQHandler);
WEAK_ALIAS(DMA2_Stream2_IRQHandler);
WEAK_ALIAS(DMA2_Stream3_IRQHandler);
WEAK_ALIAS(DMA2_Stream4_IRQHandler);
WEAK_ALIAS(OTG_FS_IRQHandler);
WEAK_ALIAS(DMA2_Stream5_IRQHandler);
WEAK_ALIAS(DMA2_Stream6_IRQHandler);
WEAK_ALIAS(DMA2_Stream7_IRQHandler);
// USART6_IRQHandler: defined in uart.cpp (see note above near USART1/2).
WEAK_ALIAS(I2C3_EV_IRQHandler);
WEAK_ALIAS(I2C3_ER_IRQHandler);
WEAK_ALIAS(FPU_IRQHandler);
WEAK_ALIAS(SPI4_IRQHandler);

__attribute__((section(".isr_vector"), used))
static const void* vector_table[] = {
    /* 0:  Initial SP         */ reinterpret_cast<void*>(&_estack),
    /* 1:  Reset              */ reinterpret_cast<void*>(Reset_Handler),
    /* 2:  NMI                */ reinterpret_cast<void*>(NMI_Handler),
    /* 3:  HardFault          */ reinterpret_cast<void*>(HardFault_Handler),
    /* 4:  MemManage          */ reinterpret_cast<void*>(MemManage_Handler),
    /* 5:  BusFault           */ reinterpret_cast<void*>(BusFault_Handler),
    /* 6:  UsageFault         */ reinterpret_cast<void*>(UsageFault_Handler),
    /* 7:  Reserved           */ nullptr,
    /* 8:  Reserved           */ nullptr,
    /* 9:  Reserved           */ nullptr,
    /* 10: Reserved           */ nullptr,
    /* 11: SVCall             */ reinterpret_cast<void*>(SVC_Handler),
    /* 12: DebugMon           */ reinterpret_cast<void*>(DebugMon_Handler),
    /* 13: Reserved           */ nullptr,
    /* 14: PendSV             */ reinterpret_cast<void*>(PendSV_Handler),
    /* 15: SysTick            */ reinterpret_cast<void*>(SysTick_Handler),
    /* IRQ0:  WWDG            */ reinterpret_cast<void*>(WWDG_IRQHandler),
    /* IRQ1:  PVD             */ reinterpret_cast<void*>(PVD_IRQHandler),
    /* IRQ2:  TAMP_STAMP      */ reinterpret_cast<void*>(TAMP_STAMP_IRQHandler),
    /* IRQ3:  RTC_WKUP        */ reinterpret_cast<void*>(RTC_WKUP_IRQHandler),
    /* IRQ4:  FLASH           */ reinterpret_cast<void*>(FLASH_IRQHandler),
    /* IRQ5:  RCC             */ reinterpret_cast<void*>(RCC_IRQHandler),
    /* IRQ6:  EXTI0           */ reinterpret_cast<void*>(EXTI0_IRQHandler),
    /* IRQ7:  EXTI1           */ reinterpret_cast<void*>(EXTI1_IRQHandler),
    /* IRQ8:  EXTI2           */ reinterpret_cast<void*>(EXTI2_IRQHandler),
    /* IRQ9:  EXTI3           */ reinterpret_cast<void*>(EXTI3_IRQHandler),
    /* IRQ10: EXTI4           */ reinterpret_cast<void*>(EXTI4_IRQHandler),
    /* IRQ11: DMA1_Stream0    */ reinterpret_cast<void*>(DMA1_Stream0_IRQHandler),
    /* IRQ12: DMA1_Stream1    */ reinterpret_cast<void*>(DMA1_Stream1_IRQHandler),
    /* IRQ13: DMA1_Stream2    */ reinterpret_cast<void*>(DMA1_Stream2_IRQHandler),
    /* IRQ14: DMA1_Stream3    */ reinterpret_cast<void*>(DMA1_Stream3_IRQHandler),
    /* IRQ15: DMA1_Stream4    */ reinterpret_cast<void*>(DMA1_Stream4_IRQHandler),
    /* IRQ16: DMA1_Stream5    */ reinterpret_cast<void*>(DMA1_Stream5_IRQHandler),
    /* IRQ17: DMA1_Stream6    */ reinterpret_cast<void*>(DMA1_Stream6_IRQHandler),
    /* IRQ18: ADC             */ reinterpret_cast<void*>(ADC_IRQHandler),
    /* IRQ19: Reserved        */ nullptr,
    /* IRQ20: Reserved        */ nullptr,
    /* IRQ21: Reserved        */ nullptr,
    /* IRQ22: Reserved        */ nullptr,
    /* IRQ23: EXTI9_5         */ reinterpret_cast<void*>(EXTI9_5_IRQHandler),
    /* IRQ24: TIM1_BRK_TIM9  */ reinterpret_cast<void*>(TIM1_BRK_TIM9_IRQHandler),
    /* IRQ25: TIM1_UP_TIM10  */ reinterpret_cast<void*>(TIM1_UP_TIM10_IRQHandler),
    /* IRQ26: TIM1_TRG_TIM11 */ reinterpret_cast<void*>(TIM1_TRG_COM_TIM11_IRQHandler),
    /* IRQ27: TIM1_CC         */ reinterpret_cast<void*>(TIM1_CC_IRQHandler),
    /* IRQ28: TIM2            */ reinterpret_cast<void*>(TIM2_IRQHandler),
    /* IRQ29: TIM3            */ reinterpret_cast<void*>(TIM3_IRQHandler),
    /* IRQ30: TIM4            */ reinterpret_cast<void*>(TIM4_IRQHandler),
    /* IRQ31: I2C1_EV         */ reinterpret_cast<void*>(I2C1_EV_IRQHandler),
    /* IRQ32: I2C1_ER         */ reinterpret_cast<void*>(I2C1_ER_IRQHandler),
    /* IRQ33: I2C2_EV         */ reinterpret_cast<void*>(I2C2_EV_IRQHandler),
    /* IRQ34: I2C2_ER         */ reinterpret_cast<void*>(I2C2_ER_IRQHandler),
    /* IRQ35: SPI1            */ reinterpret_cast<void*>(SPI1_IRQHandler),
    /* IRQ36: SPI2            */ reinterpret_cast<void*>(SPI2_IRQHandler),
    /* IRQ37: USART1          */ reinterpret_cast<void*>(USART1_IRQHandler),
    /* IRQ38: USART2          */ reinterpret_cast<void*>(USART2_IRQHandler),
    /* IRQ39: Reserved        */ nullptr,
    /* IRQ40: EXTI15_10       */ reinterpret_cast<void*>(EXTI15_10_IRQHandler),
    /* IRQ41: RTC_Alarm       */ reinterpret_cast<void*>(RTC_Alarm_IRQHandler),
    /* IRQ42: OTG_FS_WKUP    */ reinterpret_cast<void*>(OTG_FS_WKUP_IRQHandler),
    /* IRQ43: Reserved        */ nullptr,
    /* IRQ44: Reserved        */ nullptr,
    /* IRQ45: Reserved        */ nullptr,
    /* IRQ46: Reserved        */ nullptr,
    /* IRQ47: DMA1_Stream7    */ reinterpret_cast<void*>(DMA1_Stream7_IRQHandler),
    /* IRQ48: Reserved        */ nullptr,
    /* IRQ49: SDIO            */ reinterpret_cast<void*>(SDIO_IRQHandler),
    /* IRQ50: TIM5            */ reinterpret_cast<void*>(TIM5_IRQHandler),
    /* IRQ51: SPI3            */ reinterpret_cast<void*>(SPI3_IRQHandler),
    /* IRQ52: Reserved        */ nullptr,
    /* IRQ53: Reserved        */ nullptr,
    /* IRQ54: Reserved        */ nullptr,
    /* IRQ55: Reserved        */ nullptr,
    /* IRQ56: DMA2_Stream0    */ reinterpret_cast<void*>(DMA2_Stream0_IRQHandler),
    /* IRQ57: DMA2_Stream1    */ reinterpret_cast<void*>(DMA2_Stream1_IRQHandler),
    /* IRQ58: DMA2_Stream2    */ reinterpret_cast<void*>(DMA2_Stream2_IRQHandler),
    /* IRQ59: DMA2_Stream3    */ reinterpret_cast<void*>(DMA2_Stream3_IRQHandler),
    /* IRQ60: DMA2_Stream4    */ reinterpret_cast<void*>(DMA2_Stream4_IRQHandler),
    /* IRQ61: Reserved        */ nullptr,
    /* IRQ62: Reserved        */ nullptr,
    /* IRQ63: OTG_FS          */ reinterpret_cast<void*>(OTG_FS_IRQHandler),
    /* IRQ64: DMA2_Stream5    */ reinterpret_cast<void*>(DMA2_Stream5_IRQHandler),
    /* IRQ65: DMA2_Stream6    */ reinterpret_cast<void*>(DMA2_Stream6_IRQHandler),
    /* IRQ66: DMA2_Stream7    */ reinterpret_cast<void*>(DMA2_Stream7_IRQHandler),
    /* IRQ67: USART6          */ reinterpret_cast<void*>(USART6_IRQHandler),
    /* IRQ68: I2C3_EV         */ reinterpret_cast<void*>(I2C3_EV_IRQHandler),
    /* IRQ69: I2C3_ER         */ reinterpret_cast<void*>(I2C3_ER_IRQHandler),
    /* IRQ70: Reserved        */ nullptr,
    /* IRQ71: Reserved        */ nullptr,
    /* IRQ72: Reserved        */ nullptr,
    /* IRQ73: Reserved        */ nullptr,
    /* IRQ74: Reserved        */ nullptr,
    /* IRQ75: Reserved        */ nullptr,
    /* IRQ76: Reserved        */ nullptr,
    /* IRQ77: FPU             */ reinterpret_cast<void*>(FPU_IRQHandler),
    /* IRQ78: Reserved        */ nullptr,
    /* IRQ79: Reserved        */ nullptr,
    /* IRQ80: SPI4            */ reinterpret_cast<void*>(SPI4_IRQHandler),
    /* IRQ81: Reserved        */ nullptr,
};

extern "C" void Reset_Handler()
{
    uint32_t* src = &_sidata;
    uint32_t* dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }

    dst = &_sbss;
    while (dst < &_ebss) {
        *dst++ = 0;
    }

    __libc_init_array();

    main();

    for (;;) {}
}

extern "C" void Default_Handler()
{
    for (;;) {}
}
