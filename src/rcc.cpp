#include "rcc.hpp"
#include <stm32f4xx.h>

// Timeout loop counts for ~5 ms of waiting at the HSI frequency (16 MHz).
// With -Os the loop body compiles to roughly 2-3 instructions; the constants
// are deliberately generous so a marginal crystal still has time to settle.
// Reference: RM0368 §6.2.2 (HSE startup time: typically 1-2 ms).
static constexpr uint32_t HSE_TIMEOUT = 80000u;
static constexpr uint32_t PLL_TIMEOUT = 80000u;

bool rcc_init()
{
    // 1. Enable HSE and wait for it to be ready.
    RCC->CR |= RCC_CR_HSEON;
    for (uint32_t i = 0u; i < HSE_TIMEOUT; ++i) {
        if (RCC->CR & RCC_CR_HSERDY) break;
        if (i == HSE_TIMEOUT - 1u) return false;  // HSE did not start in time
    }

    // 2. Set flash latency to 2 wait states before raising SYSCLK.
    //    Required for Vdd = 3.3 V at 84 MHz (RM0368 Table 6).
    FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN | 2u;

    // 3. Configure PLL: source=HSE, M=8, N=336, P=4 (bits 01), Q=7.
    //    SYSCLK = 8 MHz / 8 * 336 / 4 = 84 MHz.
    //    USB/SDIO clock = 336 / 7 = 48 MHz (meets USB requirement).
    RCC->PLLCFGR = (8u   << RCC_PLLCFGR_PLLM_Pos)
                 | (336u << RCC_PLLCFGR_PLLN_Pos)
                 | (1u   << RCC_PLLCFGR_PLLP_Pos)   // PLLP bits 01 -> divide-by-4
                 | RCC_PLLCFGR_PLLSRC_HSE
                 | (7u   << RCC_PLLCFGR_PLLQ_Pos);

    // 4. Set bus prescalers while SYSCLK is still HSI (SW=00 after reset).
    RCC->CFGR = (0x0u << RCC_CFGR_HPRE_Pos)   // AHB  /1  = 84 MHz
              | (0x4u << RCC_CFGR_PPRE1_Pos)   // APB1 /2  = 42 MHz (bits 100)
              | (0x0u << RCC_CFGR_PPRE2_Pos);  // APB2 /1  = 84 MHz

    // 5. Enable PLL and wait for lock.
    RCC->CR |= RCC_CR_PLLON;
    for (uint32_t i = 0u; i < PLL_TIMEOUT; ++i) {
        if (RCC->CR & RCC_CR_PLLRDY) break;
        if (i == PLL_TIMEOUT - 1u) return false;  // PLL did not lock in time
    }

    // 6. Switch SYSCLK source to PLL and confirm the switch.
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    for (uint32_t i = 0u; i < PLL_TIMEOUT; ++i) {
        if ((RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_PLL) break;
        if (i == PLL_TIMEOUT - 1u) return false;  // clock switch did not complete
    }

    // 7. Update the CMSIS global used by peripheral drivers and SystemCoreClockUpdate().
    SystemCoreClock = 84000000u;
    return true;
}
