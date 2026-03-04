// stm32f4xx.h must be included before uart.hpp so that USART_TypeDef is
// fully defined (CMSIS uses an anonymous-struct typedef which cannot be
// forward-declared; see the comment in uart.hpp around __STM32F4xx_H).
#include <stm32f4xx.h>
#include "uart.hpp"
#include <cstring>

// ---------------------------------------------------------------------------
// 1 ms SysTick counter
//
// Configured once by the first Uart::init() call. Used by receive() and
// receive_line() for timeout tracking. The unsigned arithmetic in the timeout
// comparison ((s_tick_ms - start) >= timeout_ms) handles wraparound correctly.
// ---------------------------------------------------------------------------

static volatile uint32_t s_tick_ms = 0u;
static bool s_systick_init = false;

static void setup_systick()
{
    if (s_systick_init) return;
    // SysTick counts down from LOAD to 0, then fires and reloads.
    // Target: 1 ms interrupt at 84 MHz -> LOAD = 84000 - 1.
    SysTick->LOAD = (SystemCoreClock / 1000u) - 1u;
    SysTick->VAL  = 0u;
    // CLKSOURCE=1 (processor clock), TICKINT=1, ENABLE=1.
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk
                  | SysTick_CTRL_TICKINT_Msk
                  | SysTick_CTRL_ENABLE_Msk;
    s_systick_init = true;
}

extern "C" void SysTick_Handler()
{
    ++s_tick_ms;
}

// ---------------------------------------------------------------------------
// ISR dispatch table
//
// Each Uart instance registers itself here during init(). The extern "C"
// handlers at the bottom of this file look up and call the right instance.
// A null pointer means the peripheral is unused; the ISR does nothing.
// ---------------------------------------------------------------------------

static Uart* s_usart1_inst = nullptr;
static Uart* s_usart2_inst = nullptr;
static Uart* s_usart6_inst = nullptr;

// ---------------------------------------------------------------------------
// Module-level USART2 singleton (printf / syscalls.cpp support)
// ---------------------------------------------------------------------------

static Uart s_uart{USART2};

Uart& uart_instance()
{
    return s_uart;
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

Uart::Uart(USART_TypeDef* usart, uint32_t baud, Parity parity, StopBits stop)
    : usart_(usart), baud_(baud), parity_(parity), stop_(stop) {}

// ---------------------------------------------------------------------------
// resolve_periph()
//
// Maps a USART peripheral pointer to its GPIO pins, clock bits, and IRQ.
// Returns false for unsupported peripherals.
// ---------------------------------------------------------------------------

bool Uart::resolve_periph(PeriphInfo& p) const
{
    if (usart_ == USART1) {
        // USART1: PA9=TX, PA10=RX, AF7, APB2, GPIOA
        p = { GPIOA, 9u, 10u, 7u,
              RCC_AHB1ENR_GPIOAEN, RCC_APB2ENR_USART1EN, true,
              static_cast<int>(USART1_IRQn) };
        return true;
    }
    if (usart_ == USART2) {
        // USART2: PA2=TX, PA3=RX, AF7, APB1, GPIOA
        // This is the fixed mapping used by the Nucleo ST-Link virtual COM port.
        p = { GPIOA, 2u, 3u, 7u,
              RCC_AHB1ENR_GPIOAEN, RCC_APB1ENR_USART2EN, false,
              static_cast<int>(USART2_IRQn) };
        return true;
    }
    if (usart_ == USART6) {
        // USART6: PC6=TX, PC7=RX, AF8, APB2, GPIOC
        p = { GPIOC, 6u, 7u, 8u,
              RCC_AHB1ENR_GPIOCEN, RCC_APB2ENR_USART6EN, true,
              static_cast<int>(USART6_IRQn) };
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// configure_gpio()
// ---------------------------------------------------------------------------

void Uart::configure_gpio(const PeriphInfo& p)
{
    auto* gpio = static_cast<GPIO_TypeDef*>(p.gpio_port);
    uint32_t tx = p.tx_pin;
    uint32_t rx = p.rx_pin;

    // Alternate function mode (MODER = 10).
    gpio->MODER &= ~((0x3u << (tx * 2u)) | (0x3u << (rx * 2u)));
    gpio->MODER |=  ((0x2u << (tx * 2u)) | (0x2u << (rx * 2u)));

    // Push-pull output type.
    gpio->OTYPER &= ~((1u << tx) | (1u << rx));

    // High output speed (needed for baud rates above ~115200).
    gpio->OSPEEDR &= ~((0x3u << (tx * 2u)) | (0x3u << (rx * 2u)));
    gpio->OSPEEDR |=  ((0x2u << (tx * 2u)) | (0x2u << (rx * 2u)));

    // TX: no pull (actively driven push-pull).
    // RX: pull-up so the line sits high when the far end is disconnected,
    //     preventing spurious framing/noise errors on a floating input.
    //     ST RM0368 §19.3.2 recommends pull-up for the USART RX pin.
    gpio->PUPDR &= ~((0x3u << (tx * 2u)) | (0x3u << (rx * 2u)));
    gpio->PUPDR |=  (0x1u << (rx * 2u));   // pull-up on RX

    // Alternate function register. Pins 0-7 use AFR[0], pins 8-15 use AFR[1].
    uint32_t tx_bank = tx / 8u;
    uint32_t rx_bank = rx / 8u;
    uint32_t tx_pos  = (tx % 8u) * 4u;
    uint32_t rx_pos  = (rx % 8u) * 4u;
    gpio->AFR[tx_bank] &= ~(0xFu << tx_pos);
    gpio->AFR[tx_bank] |=  (p.af << tx_pos);
    gpio->AFR[rx_bank] &= ~(0xFu << rx_pos);
    gpio->AFR[rx_bank] |=  (p.af << rx_pos);
}

// ---------------------------------------------------------------------------
// configure_usart()
// ---------------------------------------------------------------------------

void Uart::configure_usart(const PeriphInfo& p)
{
    // Disable the peripheral while reconfiguring.
    usart_->CR1 &= ~USART_CR1_UE;

    // 8 data bits (M=0).
    usart_->CR1 &= ~USART_CR1_M;

    // Parity.
    switch (parity_) {
        case Parity::None:
            usart_->CR1 &= ~(USART_CR1_PCE | USART_CR1_PS);
            break;
        case Parity::Even:
            usart_->CR1 |=  USART_CR1_PCE;
            usart_->CR1 &= ~USART_CR1_PS;
            break;
        case Parity::Odd:
            usart_->CR1 |= (USART_CR1_PCE | USART_CR1_PS);
            break;
    }

    // Stop bits (default 1; bits 00 = 1, bits 10 = 2).
    usart_->CR2 &= ~USART_CR2_STOP;
    if (stop_ == StopBits::Two) {
        usart_->CR2 |= (0x2u << USART_CR2_STOP_Pos);
    }

    // Baud rate.
    //
    // BRR = round(PCLK / baud). We derive PCLK from SystemCoreClock and the
    // APB prescaler bits in RCC->CFGR rather than hard-coding 84 MHz so that
    // this driver is correct even if rcc_init() is not called (e.g. 16 MHz HSI).
    //
    // See uart_compute_brr() in uart.hpp for the formula and baud-error analysis.
    {
        uint32_t pclk;
        if (p.is_apb2) {
            uint32_t ppre2 = (RCC->CFGR >> RCC_CFGR_PPRE2_Pos) & 0x7u;
            uint32_t div   = (ppre2 < 4u) ? 1u : (1u << (ppre2 - 3u));
            pclk = SystemCoreClock / div;
        } else {
            uint32_t ppre1 = (RCC->CFGR >> RCC_CFGR_PPRE1_Pos) & 0x7u;
            uint32_t div   = (ppre1 < 4u) ? 1u : (1u << (ppre1 - 3u));
            pclk = SystemCoreClock / div;
        }
        usart_->BRR = uart_compute_brr(pclk, baud_);
    }

    // Enable RXNE interrupt. TXE interrupt is enabled on demand in send().
    usart_->CR1 |= USART_CR1_RXNEIE;

    // Enable transmitter, receiver, and the peripheral.
    usart_->CR1 |= (USART_CR1_TE | USART_CR1_RE | USART_CR1_UE);
}

// ---------------------------------------------------------------------------
// configure_nvic()
// ---------------------------------------------------------------------------

void Uart::configure_nvic(const PeriphInfo& p)
{
    // Priority 5: below SysTick (default 0) and above application-level tasks.
    // On ARMv7-M, lower numeric value = higher priority. NVIC priority grouping
    // is not changed here; the application is responsible for setting it if
    // interrupt nesting is required.
    IRQn_Type irqn = static_cast<IRQn_Type>(p.irqn);
    NVIC_SetPriority(irqn, 5u);
    NVIC_EnableIRQ(irqn);
}

// ---------------------------------------------------------------------------
// init()
// ---------------------------------------------------------------------------

bool Uart::init()
{
    if (initialized_) return true;  // idempotent: safe to call in recovery paths

    PeriphInfo p;
    if (!resolve_periph(p)) return false;  // unsupported peripheral

    // Enable GPIO and USART clocks.
    RCC->AHB1ENR |= p.rcc_ahb1_bit;
    if (p.is_apb2) {
        RCC->APB2ENR |= p.rcc_apb_bit;
    } else {
        RCC->APB1ENR |= p.rcc_apb_bit;
    }
    // Read-back to flush the AHB write buffer before accessing GPIO/USART.
    volatile uint32_t dummy = p.is_apb2 ? RCC->APB2ENR : RCC->APB1ENR;
    (void)dummy;

    configure_gpio(p);
    configure_usart(p);
    configure_nvic(p);

    // Register in the ISR dispatch table.
    if (usart_ == USART1) s_usart1_inst = this;
    else if (usart_ == USART2) s_usart2_inst = this;
    else if (usart_ == USART6) s_usart6_inst = this;

    // Enable 1 ms SysTick (shared; only configured on the first init() call).
    setup_systick();

    initialized_ = true;
    return true;
}

// ---------------------------------------------------------------------------
// send()
// ---------------------------------------------------------------------------

bool Uart::send(uint8_t byte)
{
    if (!initialized_) { ++stats_.tx_buffer_overflows; return false; }

    if (!tx_buf_.push(byte)) {
        ++stats_.tx_buffer_overflows;
        return false;
    }
    ++stats_.bytes_sent;
    usart_->CR1 |= USART_CR1_TXEIE;  // kick the ISR to drain the buffer
    return true;
}

size_t Uart::send(const char* data, size_t len)
{
    size_t enqueued = 0u;
    for (size_t i = 0u; i < len; ++i) {
        if (!send(static_cast<uint8_t>(data[i]))) break;
        ++enqueued;
    }
    return enqueued;
}

size_t Uart::send(const char* str)
{
    return send(str, strlen(str));
}

size_t Uart::send(std::string_view sv)
{
    return send(sv.data(), sv.size());
}

// ---------------------------------------------------------------------------
// receive() / rx_ready()
// ---------------------------------------------------------------------------

bool Uart::receive(uint8_t& out, uint32_t timeout_ms)
{
    uint32_t start = s_tick_ms;
    while (!rx_buf_.pop(out)) {
        // Unsigned subtraction handles the ~49-day wraparound of s_tick_ms.
        if ((s_tick_ms - start) >= timeout_ms) return false;
    }
    return true;
}

bool Uart::rx_ready() const
{
    return !rx_buf_.empty();
}

// ---------------------------------------------------------------------------
// receive_line()
// ---------------------------------------------------------------------------

bool Uart::receive_line(char* buf, size_t max_len, uint32_t timeout_ms)
{
    if (max_len == 0u) return false;

    size_t   pos   = 0u;
    uint32_t start = s_tick_ms;

    while (pos < max_len - 1u) {
        uint32_t elapsed = s_tick_ms - start;
        if (elapsed >= timeout_ms) {
            buf[pos] = '\0';
            return false;
        }
        uint8_t byte;
        if (!receive(byte, timeout_ms - elapsed)) {
            buf[pos] = '\0';
            return false;
        }
        if (byte == '\r') continue;  // silently discard carriage return
        if (byte == '\n') {
            buf[pos] = '\0';
            return true;
        }
        buf[pos++] = static_cast<char>(byte);
    }

    buf[pos] = '\0';
    return false;  // max_len reached before newline
}

// ---------------------------------------------------------------------------
// get_and_clear_errors()
// ---------------------------------------------------------------------------

Uart::Errors Uart::get_and_clear_errors()
{
    uint32_t sr = usart_->SR;
    Errors e{};
    e.overrun = (sr & USART_SR_ORE) != 0u;
    e.framing = (sr & USART_SR_FE)  != 0u;
    e.noise   = (sr & USART_SR_NE)  != 0u;

    if (e.overrun) ++stats_.overrun_errors;
    if (e.framing) ++stats_.framing_errors;
    if (e.noise)   ++stats_.noise_errors;

    if (e.overrun || e.framing || e.noise) {
        // Mandatory SR-then-DR read sequence (RM0368 §19.3.4).
        // Reading DR clears RXNE and discards the corrupt byte.
        (void)usart_->DR;
    }
    return e;
}

// ---------------------------------------------------------------------------
// irq_handler()
// ---------------------------------------------------------------------------

void Uart::irq_handler()
{
    // Clear ORE/FE/NE first. If any were set, DR has been read inside
    // get_and_clear_errors() and RXNE is now clear; skip the RXNE branch.
    Errors err = get_and_clear_errors();

    uint32_t sr  = usart_->SR;
    uint32_t cr1 = usart_->CR1;

    if (!err.overrun && !err.framing && !err.noise) {
        if ((cr1 & USART_CR1_RXNEIE) && (sr & USART_SR_RXNE)) {
            uint8_t byte = static_cast<uint8_t>(usart_->DR & 0xFFu);
            if (!rx_buf_.push(byte)) {
                ++stats_.rx_buffer_overflows;
            } else {
                ++stats_.bytes_received;
            }
        }
    }

    if ((cr1 & USART_CR1_TXEIE) && (sr & USART_SR_TXE)) {
        uint8_t byte;
        if (tx_buf_.pop(byte)) {
            usart_->DR = byte;
        } else {
            // TX buffer drained: disable TXE interrupt until the next send().
            usart_->CR1 &= ~USART_CR1_TXEIE;
        }
    }
}

// ---------------------------------------------------------------------------
// get_stats() / reset_stats()
// ---------------------------------------------------------------------------

Uart::Stats Uart::get_stats() const
{
    return stats_;
}

void Uart::reset_stats()
{
    stats_ = Stats{};
}

// ---------------------------------------------------------------------------
// flush_tx()
// ---------------------------------------------------------------------------

bool Uart::flush_tx(uint32_t timeout_ms)
{
    uint32_t start = s_tick_ms;

    // Wait for the TX ring buffer to drain (ISR pops bytes into the shift reg).
    while (!tx_buf_.empty()) {
        if ((s_tick_ms - start) >= timeout_ms) return false;
    }

    // Wait for the shift register to finish sending the last byte.
    // TC goes low when DR is written and high when the last stop bit leaves.
    while (!(usart_->SR & USART_SR_TC)) {
        if ((s_tick_ms - start) >= timeout_ms) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// extern "C" ISR entry points
//
// Each handler checks for a registered instance before dispatching so that
// unused peripherals do not cause a fault if their IRQ fires spuriously.
// ---------------------------------------------------------------------------

extern "C" void USART1_IRQHandler()
{
    if (s_usart1_inst) s_usart1_inst->irq_handler();
}

extern "C" void USART2_IRQHandler()
{
    if (s_usart2_inst) s_usart2_inst->irq_handler();
}

extern "C" void USART6_IRQHandler()
{
    if (s_usart6_inst) s_usart6_inst->irq_handler();
}
