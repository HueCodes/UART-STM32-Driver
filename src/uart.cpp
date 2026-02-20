#include "uart.hpp"
#include <stm32f4xx.h>   // CMSIS device header - defines RCC, GPIOA, USART2, etc.

// ---------------------------------------------------------------------------
// Peripheral base addresses and bit definitions used below are all from the
// CMSIS stm32f4xx.h header; no ST HAL is involved.
// ---------------------------------------------------------------------------

Uart::Uart(uint32_t baud, Parity parity, StopBits stop)
    : baud_(baud), parity_(parity), stop_(stop) {}

void Uart::init()
{
    // 1. Enable clocks: GPIOA (AHB1) and USART2 (APB1)
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;

    // Short delay to let clocks stabilise (read-back idiom)
    volatile uint32_t dummy = RCC->APB1ENR;
    (void)dummy;

    // 2. Configure PA2 and PA3 as Alternate Function, push-pull, no pull, high speed
    //    MODER: 10 = alternate function
    GPIOA->MODER &= ~((0x3u << (2 * 2)) | (0x3u << (3 * 2)));
    GPIOA->MODER |=  ((0x2u << (2 * 2)) | (0x2u << (3 * 2)));

    //    OTYPER: 0 = push-pull (default, but be explicit)
    GPIOA->OTYPER &= ~((1u << 2) | (1u << 3));

    //    OSPEEDR: 10 = high speed
    GPIOA->OSPEEDR &= ~((0x3u << (2 * 2)) | (0x3u << (3 * 2)));
    GPIOA->OSPEEDR |=  ((0x2u << (2 * 2)) | (0x2u << (3 * 2)));

    //    PUPDR: 00 = no pull (UART lines are driven)
    GPIOA->PUPDR &= ~((0x3u << (2 * 2)) | (0x3u << (3 * 2)));

    //    AFR: AF7 (USART2) for both pins
    //    AFR[0] covers pins 0-7; each pin gets 4 bits.
    GPIOA->AFR[0] &= ~((0xFu << (4 * 2)) | (0xFu << (4 * 3)));
    GPIOA->AFR[0] |=  ((0x7u << (4 * 2)) | (0x7u << (4 * 3)));

    // 3. Configure USART2
    //    Disable USART while configuring
    USART2->CR1 &= ~USART_CR1_UE;

    //    Word length: 8 data bits (M = 0, default)
    USART2->CR1 &= ~USART_CR1_M;

    //    Parity
    switch (parity_) {
        case Parity::None:
            USART2->CR1 &= ~(USART_CR1_PCE | USART_CR1_PS);
            break;
        case Parity::Even:
            USART2->CR1 |=  USART_CR1_PCE;
            USART2->CR1 &= ~USART_CR1_PS;
            break;
        case Parity::Odd:
            USART2->CR1 |= (USART_CR1_PCE | USART_CR1_PS);
            break;
    }

    //    Stop bits
    USART2->CR2 &= ~USART_CR2_STOP;
    if (stop_ == StopBits::Two) {
        USART2->CR2 |= (0x2u << USART_CR2_STOP_Pos);
    }

    //    Baud rate: BRR = fPCLK1 / baud
    //    SystemCoreClock reflects the CPU clock; PCLK1 = HCLK/2 on F401 at full speed.
    //    At 84 MHz CPU, PCLK1 = 42 MHz.  For simplicity we derive from SystemCoreClock.
    //    Users running at non-default speeds should adjust the divisor below.
    //
    //    USART2 hangs off APB1 which runs at SystemCoreClock/4 on F401 (max 42 MHz).
    //    The APB1 prescaler is stored in RCC->CFGR[PPRE1]. We read it to be accurate.
    {
        uint32_t apb1_pre_bits = (RCC->CFGR >> RCC_CFGR_PPRE1_Pos) & 0x7u;
        uint32_t apb1_div;
        if (apb1_pre_bits < 4) {
            apb1_div = 1;
        } else {
            // 100 -> /2, 101 -> /4, 110 -> /8, 111 -> /16
            apb1_div = 1u << (apb1_pre_bits - 3u);
        }
        uint32_t pclk1 = SystemCoreClock / apb1_div;
        USART2->BRR = (pclk1 + baud_ / 2) / baud_;  // round to nearest
    }

    //    Enable transmitter, receiver, and USART
    USART2->CR1 |= (USART_CR1_TE | USART_CR1_RE | USART_CR1_UE);
}

void Uart::send(uint8_t byte)
{
    // Wait until the transmit data register is empty
    while (!(USART2->SR & USART_SR_TXE)) {}
    USART2->DR = byte;
}

void Uart::send(const char* str)
{
    while (*str) {
        send(static_cast<uint8_t>(*str++));
    }
}

uint8_t Uart::receive()
{
    // Block until a byte has been received
    while (!(USART2->SR & USART_SR_RXNE)) {}
    return static_cast<uint8_t>(USART2->DR & 0xFFu);
}

bool Uart::rx_ready() const
{
    return (USART2->SR & USART_SR_RXNE) != 0;
}
