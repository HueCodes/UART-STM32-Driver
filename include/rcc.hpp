#pragma once

#include <cstdint>

/**
 * @file rcc.hpp
 * @brief Clock-tree initialisation for STM32F401.
 */

/**
 * @brief Switch SYSCLK to HSE-PLL at 84 MHz.
 *
 * Clock tree after a successful call:
 *   - Source : HSE (8 MHz crystal on Nucleo-F401RE)
 *   - PLL    : M=8, N=336, P=4  ->  SYSCLK = 84 MHz
 *   - AHB    : /1  = 84 MHz (HCLK)
 *   - APB1   : /2  = 42 MHz (PCLK1, used by USART2)
 *   - APB2   : /1  = 84 MHz (PCLK2, used by USART1 / USART6)
 *
 * On success, updates @c SystemCoreClock to 84 000 000.
 *
 * The function times out after approximately 5 ms of waiting for HSE or PLL
 * lock rather than spinning forever. The caller should halt or fall back to
 * the internal HSI oscillator on failure.
 *
 * @return true on success; false if HSE failed to start or PLL failed to lock.
 */
bool rcc_init();
