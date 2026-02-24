#include "uart.hpp"
#include <stm32f4xx.h>
#include <cstring>

// ---------------------------------------------------------------------------
// Static member definitions
// ---------------------------------------------------------------------------

RingBuffer<uint8_t, 256> Uart::tx_buf_;
RingBuffer<uint8_t, 256> Uart::rx_buf_;

// ---------------------------------------------------------------------------
// Module-level singleton
// ---------------------------------------------------------------------------

static Uart s_uart;

Uart& uart_instance()
{
    return s_uart;
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

Uart::Uart(uint32_t baud, Parity parity, StopBits stop)
    : baud_(baud), parity_(parity), stop_(stop) {}

// ---------------------------------------------------------------------------
// init()
// ---------------------------------------------------------------------------

void Uart::init()
{
    // 1. Enable clocks: GPIOA (AHB1) and USART2 (APB1).
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;

    // Read-back to let the clock enable propagate.
    volatile uint32_t dummy = RCC->APB1ENR;
    (void)dummy;

    // 2. Configure PA2 (TX) and PA3 (RX): AF7, push-pull, high speed, no pull.
    GPIOA->MODER &= ~((0x3u << (2 * 2)) | (0x3u << (3 * 2)));
    GPIOA->MODER |=  ((0x2u << (2 * 2)) | (0x2u << (3 * 2)));
    GPIOA->OTYPER &= ~((1u << 2) | (1u << 3));
    GPIOA->OSPEEDR &= ~((0x3u << (2 * 2)) | (0x3u << (3 * 2)));
    GPIOA->OSPEEDR |=  ((0x2u << (2 * 2)) | (0x2u << (3 * 2)));
    GPIOA->PUPDR &= ~((0x3u << (2 * 2)) | (0x3u << (3 * 2)));
    GPIOA->AFR[0] &= ~((0xFu << (4 * 2)) | (0xFu << (4 * 3)));
    GPIOA->AFR[0] |=  ((0x7u << (4 * 2)) | (0x7u << (4 * 3)));

    // 3. Configure USART2.
    USART2->CR1 &= ~USART_CR1_UE;
    USART2->CR1 &= ~USART_CR1_M;  // 8 data bits

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

    USART2->CR2 &= ~USART_CR2_STOP;
    if (stop_ == StopBits::Two) {
        USART2->CR2 |= (0x2u << USART_CR2_STOP_Pos);
    }

    {
        uint32_t ppre1 = (RCC->CFGR >> RCC_CFGR_PPRE1_Pos) & 0x7u;
        uint32_t div   = (ppre1 < 4u) ? 1u : (1u << (ppre1 - 3u));
        uint32_t pclk1 = SystemCoreClock / div;
        USART2->BRR = (pclk1 + baud_ / 2u) / baud_;
    }

    // 4. Enable RXNE interrupt. TXE interrupt is enabled on demand by send().
    USART2->CR1 |= USART_CR1_RXNEIE;

    // 5. Enable TX, RX, and the USART.
    USART2->CR1 |= (USART_CR1_TE | USART_CR1_RE | USART_CR1_UE);

    // 6. Enable USART2 IRQ at priority 5 in the NVIC.
    NVIC_SetPriority(USART2_IRQn, 5);
    NVIC_EnableIRQ(USART2_IRQn);
}

// ---------------------------------------------------------------------------
// send()
// ---------------------------------------------------------------------------

void Uart::send(uint8_t byte)
{
    while (!tx_buf_.push(byte)) {}
    USART2->CR1 |= USART_CR1_TXEIE;
}

void Uart::send(const char* str)
{
    send(str, strlen(str));
}

void Uart::send(const char* str, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        send(static_cast<uint8_t>(str[i]));
    }
}

void Uart::send(std::string_view sv)
{
    send(sv.data(), sv.size());
}

// ---------------------------------------------------------------------------
// receive() / rx_ready()
// ---------------------------------------------------------------------------

uint8_t Uart::receive()
{
    uint8_t byte;
    while (!rx_buf_.pop(byte)) {}
    return byte;
}

bool Uart::rx_ready() const
{
    return !rx_buf_.empty();
}

// ---------------------------------------------------------------------------
// get_and_clear_errors()
// ---------------------------------------------------------------------------

Uart::Errors Uart::get_and_clear_errors()
{
    uint32_t sr = USART2->SR;
    Errors e{};
    e.overrun = (sr & USART_SR_ORE) != 0;
    e.framing = (sr & USART_SR_FE)  != 0;
    e.noise   = (sr & USART_SR_NE)  != 0;
    if (e.overrun || e.framing || e.noise) {
        // Complete the mandatory SR-then-DR sequence to clear error flags.
        // This also clears RXNE; the errored byte is discarded.
        (void)USART2->DR;
    }
    return e;
}

// ---------------------------------------------------------------------------
// irq_handler()
// ---------------------------------------------------------------------------

void Uart::irq_handler()
{
    // Clear any ORE/FE/NE flags first. If errors were present, DR has been
    // read inside get_and_clear_errors() and RXNE is now clear.
    get_and_clear_errors();

    uint32_t sr  = USART2->SR;
    uint32_t cr1 = USART2->CR1;

    if ((cr1 & USART_CR1_RXNEIE) && (sr & USART_SR_RXNE)) {
        uint8_t byte = static_cast<uint8_t>(USART2->DR & 0xFFu);
        rx_buf_.push(byte);
    }

    if ((cr1 & USART_CR1_TXEIE) && (sr & USART_SR_TXE)) {
        uint8_t byte;
        if (tx_buf_.pop(byte)) {
            USART2->DR = byte;
        } else {
            // TX buffer drained; disable TXE interrupt until next send().
            USART2->CR1 &= ~USART_CR1_TXEIE;
        }
    }
}

// ---------------------------------------------------------------------------
// ISR entry point
// ---------------------------------------------------------------------------

extern "C" void USART2_IRQHandler()
{
    Uart::irq_handler();
}
