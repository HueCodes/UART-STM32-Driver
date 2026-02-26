#pragma once

// Minimal mock of STM32F4xx CMSIS headers for host-side unit testing.
// Only the register fields and bit masks used by uart.cpp / rcc.cpp are defined.
// Values are chosen to match the real hardware where the test logic cares about
// exact bit positions; otherwise they are arbitrary non-zero sentinels.

#include <cstdint>

// ---------------------------------------------------------------------------
// System clock variable (normally provided by CMSIS system_stm32f4xx.c)
// ---------------------------------------------------------------------------

inline uint32_t SystemCoreClock = 84000000u;

// ---------------------------------------------------------------------------
// IRQ numbers (IRQn_Type)
// ---------------------------------------------------------------------------

enum IRQn_Type : int {
    USART1_IRQn = 37,
    USART2_IRQn = 38,
    USART6_IRQn = 71,
};

// ---------------------------------------------------------------------------
// NVIC stubs (no-ops on host)
// ---------------------------------------------------------------------------

inline void NVIC_SetPriority(IRQn_Type, uint32_t) {}
inline void NVIC_EnableIRQ(IRQn_Type)              {}

// ---------------------------------------------------------------------------
// SysTick register struct (not used by tests but needed for compilation)
// ---------------------------------------------------------------------------

struct SysTick_Type {
    uint32_t CTRL;
    uint32_t LOAD;
    uint32_t VAL;
};

inline SysTick_Type SysTick_mock{};
#define SysTick (&SysTick_mock)

static constexpr uint32_t SysTick_CTRL_CLKSOURCE_Msk = (1u << 2);
static constexpr uint32_t SysTick_CTRL_TICKINT_Msk   = (1u << 1);
static constexpr uint32_t SysTick_CTRL_ENABLE_Msk    = (1u << 0);

// ---------------------------------------------------------------------------
// USART register struct and bit masks
// ---------------------------------------------------------------------------

struct USART_TypeDef {
    uint32_t SR;
    uint32_t DR;
    uint32_t BRR;
    uint32_t CR1;
    uint32_t CR2;
    uint32_t CR3;
    uint32_t GTPR;
};

// SR bits
static constexpr uint32_t USART_SR_ORE   = (1u << 3);
static constexpr uint32_t USART_SR_NE    = (1u << 2);
static constexpr uint32_t USART_SR_FE    = (1u << 1);
static constexpr uint32_t USART_SR_RXNE  = (1u << 5);
static constexpr uint32_t USART_SR_TXE   = (1u << 7);
static constexpr uint32_t USART_SR_TC    = (1u << 6);

// CR1 bits
static constexpr uint32_t USART_CR1_UE     = (1u << 13);
static constexpr uint32_t USART_CR1_M      = (1u << 12);
static constexpr uint32_t USART_CR1_PCE    = (1u << 10);
static constexpr uint32_t USART_CR1_PS     = (1u << 9);
static constexpr uint32_t USART_CR1_RXNEIE = (1u << 5);
static constexpr uint32_t USART_CR1_TXEIE  = (1u << 7);
static constexpr uint32_t USART_CR1_TE     = (1u << 3);
static constexpr uint32_t USART_CR1_RE     = (1u << 2);

// CR2 bits
static constexpr uint32_t USART_CR2_STOP_Pos = 12u;
static constexpr uint32_t USART_CR2_STOP     = (0x3u << USART_CR2_STOP_Pos);

// Mock peripheral instances (global objects; tests take their address).
inline USART_TypeDef USART1_mock{};
inline USART_TypeDef USART2_mock{};
inline USART_TypeDef USART6_mock{};

#define USART1 (&USART1_mock)
#define USART2 (&USART2_mock)
#define USART6 (&USART6_mock)

// ---------------------------------------------------------------------------
// GPIO register struct
// ---------------------------------------------------------------------------

struct GPIO_TypeDef {
    uint32_t MODER;
    uint32_t OTYPER;
    uint32_t OSPEEDR;
    uint32_t PUPDR;
    uint32_t IDR;
    uint32_t ODR;
    uint32_t BSRR;
    uint32_t LCKR;
    uint32_t AFR[2];
};

inline GPIO_TypeDef GPIOA_mock{};
inline GPIO_TypeDef GPIOC_mock{};

#define GPIOA (&GPIOA_mock)
#define GPIOC (&GPIOC_mock)

// ---------------------------------------------------------------------------
// RCC register struct and bit masks
// ---------------------------------------------------------------------------

struct RCC_TypeDef {
    uint32_t CR;
    uint32_t PLLCFGR;
    uint32_t CFGR;
    uint32_t CIR;
    uint32_t AHB1RSTR;
    uint32_t AHB2RSTR;
    uint32_t reserved0[2];
    uint32_t APB1RSTR;
    uint32_t APB2RSTR;
    uint32_t reserved1[2];
    uint32_t AHB1ENR;
    uint32_t AHB2ENR;
    uint32_t reserved2[2];
    uint32_t APB1ENR;
    uint32_t APB2ENR;
};

// RCC->CFGR prescaler positions (APB1/APB2 not divided -> bits = 0)
static constexpr uint32_t RCC_CFGR_PPRE1_Pos = 10u;
static constexpr uint32_t RCC_CFGR_PPRE2_Pos = 13u;

// AHB1ENR bits
static constexpr uint32_t RCC_AHB1ENR_GPIOAEN = (1u << 0);
static constexpr uint32_t RCC_AHB1ENR_GPIOCEN = (1u << 2);

// APB1ENR bits
static constexpr uint32_t RCC_APB1ENR_USART2EN = (1u << 17);

// APB2ENR bits
static constexpr uint32_t RCC_APB2ENR_USART1EN = (1u << 4);
static constexpr uint32_t RCC_APB2ENR_USART6EN = (1u << 5);

inline RCC_TypeDef RCC_mock{};
#define RCC (&RCC_mock)

// ---------------------------------------------------------------------------
// FLASH register struct (only ACR used)
// ---------------------------------------------------------------------------

struct FLASH_TypeDef {
    uint32_t ACR;
};

static constexpr uint32_t FLASH_ACR_PRFTEN = (1u << 8);
static constexpr uint32_t FLASH_ACR_ICEN   = (1u << 9);
static constexpr uint32_t FLASH_ACR_DCEN   = (1u << 10);

inline FLASH_TypeDef FLASH_mock{};
#define FLASH (&FLASH_mock)

// RCC->CR bits (for rcc_init tests)
static constexpr uint32_t RCC_CR_HSEON  = (1u << 16);
static constexpr uint32_t RCC_CR_HSERDY = (1u << 17);
static constexpr uint32_t RCC_CR_PLLON  = (1u << 24);
static constexpr uint32_t RCC_CR_PLLRDY = (1u << 25);

static constexpr uint32_t RCC_CFGR_SW_PLL  = (0x2u << 0);
static constexpr uint32_t RCC_CFGR_SWS     = (0x3u << 2);
static constexpr uint32_t RCC_CFGR_SWS_PLL = (0x2u << 2);
static constexpr uint32_t RCC_CFGR_HPRE_Pos  = 4u;

static constexpr uint32_t RCC_PLLCFGR_PLLM_Pos   = 0u;
static constexpr uint32_t RCC_PLLCFGR_PLLN_Pos   = 6u;
static constexpr uint32_t RCC_PLLCFGR_PLLP_Pos   = 16u;
static constexpr uint32_t RCC_PLLCFGR_PLLSRC_HSE = (1u << 22);
static constexpr uint32_t RCC_PLLCFGR_PLLQ_Pos   = 24u;
