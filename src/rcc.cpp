#include "rcc.hpp"
#include <stm32f4xx.h>

void rcc_init()
{
    // 1. Enable HSE and wait for it to be ready.
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY)) {}

    // 2. Set flash latency to 2 wait states before raising SYSCLK.
    //    Required for 84 MHz at 3.3 V (see F401 reference manual Table 6).
    FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN | 2u;

    // 3. Configure PLL: source=HSE, M=8, N=336, P=4 (bits=01), Q=7.
    //    SYSCLK = 8 MHz / 8 * 336 / 4 = 84 MHz.
    RCC->PLLCFGR = (8u   << RCC_PLLCFGR_PLLM_Pos)
                 | (336u << RCC_PLLCFGR_PLLN_Pos)
                 | (1u   << RCC_PLLCFGR_PLLP_Pos)  // 01 -> /4
                 | RCC_PLLCFGR_PLLSRC_HSE
                 | (7u   << RCC_PLLCFGR_PLLQ_Pos);

    // 4. Set bus prescalers: AHB/1, APB1/2, APB2/1.
    //    Write CFGR while SW still selects HSI (SW=00 after reset).
    RCC->CFGR = (0x0u << RCC_CFGR_HPRE_Pos)   // AHB  /1
              | (0x4u << RCC_CFGR_PPRE1_Pos)   // APB1 /2 (bits 100)
              | (0x0u << RCC_CFGR_PPRE2_Pos);  // APB2 /1

    // 5. Enable PLL and wait for lock.
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) {}

    // 6. Switch SYSCLK source to PLL.
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {}

    // 7. Update the CMSIS core clock variable.
    SystemCoreClock = 84000000u;
}
