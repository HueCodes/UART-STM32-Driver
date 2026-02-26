#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>
#include "ring_buffer.hpp"

/**
 * @file uart.hpp
 * @brief Interrupt-driven bare-metal UART driver for STM32F401.
 *
 * ## Supported peripherals
 * | Peripheral | TX   | RX    | AF | Bus  | IRQ      |
 * |------------|------|-------|----|------|----------|
 * | USART1     | PA9  | PA10  | 7  | APB2 | IRQ37    |
 * | USART2     | PA2  | PA3   | 7  | APB1 | IRQ38    |
 * | USART6     | PC6  | PC7   | 8  | APB2 | IRQ71    |
 *
 * ## Architecture
 * TX is fully non-blocking: bytes are queued into a 256-byte ring buffer and
 * drained by the TXE interrupt. send() returns immediately even at full speed.
 *
 * RX is interrupt-driven with an optional millisecond-resolution timeout.
 * receive() and receive_line() rely on a 1 ms SysTick configured by init().
 *
 * ## ISR / thread safety
 * The ring buffer uses the SPSC volatile-index pattern (see ring_buffer.hpp).
 * No mutex is required. Stats counters are written only from ISR context and
 * read (never written) from main context; on ARMv7-M, aligned 32-bit reads
 * are atomic, so no torn reads occur for individual fields.
 *
 * ## No heap allocation
 * All buffers are statically allocated. No exceptions. No RTOS dependency.
 */

// Forward declaration; definition is in uart.cpp.
struct USART_TypeDef;

// ---------------------------------------------------------------------------
// Status codes
// ---------------------------------------------------------------------------

/**
 * @brief Status codes returned by most public Uart methods.
 */
enum class UartStatus : uint8_t {
    Ok,              ///< Operation completed successfully.
    BufferFull,      ///< TX ring buffer is full; byte(s) were not enqueued.
    Timeout,         ///< Operation did not complete within the allowed time.
    NotInitialized,  ///< init() has not been called successfully.
    InvalidArg,      ///< An argument (e.g. peripheral pointer) is not supported.
    Overrun,         ///< Hardware overrun error (ORE) detected.
    Framing,         ///< Hardware framing error (FE) detected.
    Noise,           ///< Hardware noise error (NE) detected.
};

// ---------------------------------------------------------------------------
// Uart class
// ---------------------------------------------------------------------------

class Uart {
public:
    /** @brief Parity mode. */
    enum class Parity   : uint8_t { None, Even, Odd };
    /** @brief Stop-bit count. */
    enum class StopBits : uint8_t { One, Two };

    /**
     * @brief Hardware error flags latched since the last call to
     *        get_and_clear_errors().
     */
    struct Errors {
        bool overrun; ///< Overrun: new byte arrived before previous was read.
        bool framing; ///< Framing: stop bit was not detected at expected time.
        bool noise;   ///< Noise: noise was detected on the RX line.
    };

    /**
     * @brief Cumulative driver statistics. Reset via reset_stats().
     *
     * Counters are written from ISR context and read from main context.
     * Individual 32-bit field reads are atomic on ARMv7-M.
     */
    struct Stats {
        uint32_t bytes_sent;          ///< Bytes successfully enqueued for TX.
        uint32_t bytes_received;      ///< Bytes pushed into RX buffer by ISR.
        uint32_t tx_buffer_overflows; ///< TX send() calls dropped (buffer full).
        uint32_t rx_buffer_overflows; ///< RX bytes dropped by ISR (buffer full).
        uint32_t overrun_errors;      ///< Hardware ORE events.
        uint32_t framing_errors;      ///< Hardware FE events.
        uint32_t noise_errors;        ///< Hardware NE events.
    };

    /**
     * @brief Construct a UART driver bound to a specific peripheral.
     *
     * @param usart  Peripheral register base (USART1, USART2, or USART6).
     * @param baud   Desired baud rate in bits/s (e.g. 115200).
     * @param parity Parity mode. Default: None.
     * @param stop   Stop-bit count. Default: One.
     */
    explicit Uart(USART_TypeDef* usart,
                  uint32_t       baud   = 115200,
                  Parity         parity = Parity::None,
                  StopBits       stop   = StopBits::One);

    /**
     * @brief Configure GPIO, RCC clocks, the USART peripheral, and NVIC.
     *
     * Enables a 1 ms SysTick on the first successful call (shared across all
     * Uart instances). Registers this instance in the ISR dispatch table.
     *
     * @return true on success.
     * @return false if the peripheral is not USART1/2/6, if this instance has
     *         already been initialised, or if the peripheral clock is off.
     * @note Must be called once before any send() or receive() call.
     */
    bool init();

    /**
     * @brief Enqueue one byte for transmission (non-blocking).
     *
     * @param byte Value to transmit.
     * @return true if the byte was enqueued; false if the TX buffer was full.
     * @note Enables the TXE interrupt so the ISR drains the buffer.
     */
    bool send(uint8_t byte);

    /**
     * @brief Enqueue a raw buffer for transmission (non-blocking).
     *
     * Bytes are enqueued until the buffer fills up. Returns the number of
     * bytes actually enqueued, which may be less than @p len.
     *
     * @param data Pointer to the source buffer. Must not be nullptr.
     * @param len  Number of bytes to send.
     * @return Number of bytes enqueued.
     */
    size_t send(const char* data, size_t len);

    /**
     * @brief Enqueue a null-terminated string for transmission (non-blocking).
     *
     * @param str Null-terminated string. Must not be nullptr.
     * @return Number of bytes enqueued.
     */
    size_t send(const char* str);

    /**
     * @brief Enqueue a string_view for transmission (non-blocking).
     *
     * @param sv String view to send.
     * @return Number of bytes enqueued.
     */
    size_t send(std::string_view sv);

    /**
     * @brief Wait for one received byte with a millisecond timeout.
     *
     * Spins (without yielding) until the RX ring buffer contains a byte or
     * @p timeout_ms milliseconds elapse as measured by the 1 ms SysTick.
     *
     * @param[out] out        Receives the byte on success.
     * @param      timeout_ms Maximum wait in milliseconds. Pass 0 for an
     *                        immediate poll with no blocking.
     * @return true if a byte was received; false on timeout.
     * @pre init() must have been called successfully.
     */
    bool receive(uint8_t& out, uint32_t timeout_ms = 1000);

    /**
     * @brief Non-blocking poll: true if the RX buffer is non-empty.
     */
    bool rx_ready() const;

    /**
     * @brief Read bytes until a newline ('\\n') or timeout.
     *
     * Characters are appended to @p buf until '\\n' is received, @p max_len-1
     * bytes have been stored, or @p timeout_ms elapses. The buffer is always
     * null-terminated. '\\r' characters are silently discarded.
     *
     * @param[out] buf        Destination buffer. Must be at least @p max_len bytes.
     * @param      max_len    Capacity of @p buf including the null terminator.
     * @param      timeout_ms Maximum total wait in milliseconds.
     * @return true if '\\n' was received before timeout or buffer full.
     * @return false on timeout; @p buf contains whatever was received so far.
     */
    bool receive_line(char* buf, size_t max_len, uint32_t timeout_ms = 1000);

    /**
     * @brief Read and clear hardware error flags (ORE / FE / NE).
     *
     * Performs the mandatory STM32F4 SR-then-DR read sequence to clear error
     * flags. If any flag is set the corrupt RX byte is discarded. The relevant
     * Stats counters are incremented.
     *
     * @return Snapshot of the error flags at the time of the call.
     * @note Also called internally from irq_handler() on every interrupt.
     */
    Errors get_and_clear_errors();

    /**
     * @brief ISR entry point. Called from the peripheral's extern "C" handler.
     *
     * Handles RXNE (push received byte) and TXE (pop next TX byte or disable
     * TXE interrupt if the buffer is empty). Calls get_and_clear_errors() at
     * the start of every interrupt to clear ORE/FE/NE flags.
     *
     * @note Do not call from application code.
     */
    void irq_handler();

    /**
     * @brief Return a snapshot of the cumulative statistics counters.
     */
    Stats get_stats() const;

    /**
     * @brief Reset all statistics counters to zero.
     */
    void reset_stats();

private:
    USART_TypeDef* usart_;
    uint32_t       baud_;
    Parity         parity_;
    StopBits       stop_;
    bool           initialized_{false};
    Stats          stats_{};

    RingBuffer<uint8_t, 256> tx_buf_;
    RingBuffer<uint8_t, 256> rx_buf_;

    // Peripheral descriptor resolved in init().
    struct PeriphInfo {
        void*     gpio_port;    ///< GPIO_TypeDef* for TX/RX pins (void* avoids include).
        uint32_t  tx_pin;       ///< TX pin index 0-15.
        uint32_t  rx_pin;       ///< RX pin index 0-15.
        uint32_t  af;           ///< Alternate function number (7 or 8).
        uint32_t  rcc_ahb1_bit; ///< Bit in RCC->AHB1ENR for the GPIO clock.
        uint32_t  rcc_apb_bit;  ///< Bit in RCC->APB1ENR or APB2ENR for USART clock.
        bool      is_apb2;      ///< true = APB2 (USART1/6), false = APB1 (USART2).
        int       irqn;         ///< NVIC IRQn_Type cast to int.
    };

    bool resolve_periph(PeriphInfo& out) const;
    void configure_gpio(const PeriphInfo& p);
    void configure_usart(const PeriphInfo& p);
    void configure_nvic(const PeriphInfo& p);
};

// ---------------------------------------------------------------------------
// Module-level USART2 singleton (used by syscalls.cpp / printf support)
// ---------------------------------------------------------------------------

/**
 * @brief Return the module-level USART2 singleton.
 *
 * This is a convenience accessor used by syscalls.cpp to route printf output
 * through USART2. Application code that needs multiple UARTs should
 * instantiate Uart objects directly rather than using this function.
 */
Uart& uart_instance();

// ---------------------------------------------------------------------------
// BRR calculation (free function — host-testable without hardware)
// ---------------------------------------------------------------------------

/**
 * @brief Compute the USART BRR register value for a given clock and baud rate.
 *
 * Formula: BRR = (pclk + baud/2) / baud   (rounds to nearest integer).
 * The +baud/2 term avoids accumulating a systematic negative bias.
 *
 * Baud error = |BRR * baud - pclk| / pclk. Must be < 2% per UART spec.
 *
 * @param pclk Peripheral clock in Hz (PCLK1 for USART2, PCLK2 for USART1/6).
 * @param baud Desired baud rate in bits/s.
 * @return Value to write to USART->BRR.
 */
constexpr uint32_t uart_compute_brr(uint32_t pclk, uint32_t baud)
{
    return (pclk + baud / 2u) / baud;
}
