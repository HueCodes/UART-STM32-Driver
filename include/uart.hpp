#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>
#include "ring_buffer.hpp"

// Bare-metal UART driver for USART2 on STM32F4xx Nucleo boards.
//
// Hardware mapping (fixed by Nucleo ST-Link virtual COM port):
//   PA2  -- USART2_TX  (AF7)
//   PA3  -- USART2_RX  (AF7)
//
// TX is interrupt-driven (non-blocking send via ring buffer).
// RX is interrupt-driven; receive() blocks until data is available.

class Uart {
public:
    enum class Parity  { None, Even, Odd };
    enum class StopBits { One, Two };

    struct Errors {
        bool overrun;
        bool framing;
        bool noise;
    };

    explicit Uart(uint32_t baud    = 115200,
                  Parity   parity  = Parity::None,
                  StopBits stop    = StopBits::One);

    // Configure clocks, GPIO, USART2, and NVIC. Call once before use.
    void init();

    // Non-blocking transmit: pushes to TX ring buffer, enables TXE interrupt.
    void send(uint8_t byte);
    void send(const char* str);
    void send(const char* str, size_t len);
    void send(std::string_view sv);

    // Blocking receive: spins until RX ring buffer has data.
    uint8_t receive();

    // Non-blocking poll: true if RX ring buffer is non-empty.
    bool rx_ready() const;

    // Read and clear ORE/FE/NE via the mandatory SR-then-DR sequence.
    static Errors get_and_clear_errors();

    // Called from extern "C" USART2_IRQHandler in uart.cpp.
    static void irq_handler();

private:
    uint32_t baud_;
    Parity   parity_;
    StopBits stop_;

    static RingBuffer<uint8_t, 256> tx_buf_;
    static RingBuffer<uint8_t, 256> rx_buf_;
};

// Returns the module-level singleton Uart (defined in uart.cpp).
Uart& uart_instance();
