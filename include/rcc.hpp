#pragma once

#include <cstdint>

// Switch SYSCLK to HSE-PLL: 8 MHz HSE -> 84 MHz SYSCLK.
//   PLL: M=8, N=336, P=4 -> VCO=336 MHz, SYSCLK=84 MHz
//   APB1 = HCLK/2 = 42 MHz
//   APB2 = HCLK/1 = 84 MHz
// Updates SystemCoreClock to 84000000.
void rcc_init();
