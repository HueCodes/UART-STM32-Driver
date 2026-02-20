#pragma once

#include <cstdint>

// Bare-metal UART driver for USART2 on STM32F4xx Nucleo boards.
//
// Hardware mapping (fixed by Nucleo ST-Link virtual COM port):
//   PA2  -- USART2_TX  (AF7)
//   PA3  -- USART2_RX  (AF7)
//
// Usage:
//   Uart uart;       // defaults: 115200 8N1
//   uart.init();
//   uart.send("hello\r\n");
//   uint8_t b = uart.receive();  // blocks until a byte arrives

class Uart {
public:
    enum class Parity  { None, Even, Odd };
    enum class StopBits { One, Two };

    explicit Uart(uint32_t baud    = 115200,
                  Parity   parity  = Parity::None,
                  StopBits stop    = StopBits::One);

    // Configure clocks, GPIO, and USART2 registers. Call once before use.
    void init();

    // Blocking transmit - waits for TXE before writing each byte.
    void send(uint8_t byte);
    void send(const char* str);

    // Blocking receive - waits until RXNE is set.
    uint8_t receive();

    // Non-blocking poll - true if a received byte is waiting in DR.
    bool rx_ready() const;

private:
    uint32_t baud_;
    Parity   parity_;
    StopBits stop_;
};
