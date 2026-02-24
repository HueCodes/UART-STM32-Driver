#include "uart.hpp"
#include "rcc.hpp"

// Simple echo demo: every byte received on USART2 is sent straight back.
//
// Connect via the Nucleo's virtual COM port (ST-Link USB, shows up as
// /dev/tty.usbmodem* on macOS or /dev/ttyACM0 on Linux) at 115200 8N1.

int main()
{
    rcc_init();

    Uart& uart = uart_instance();
    uart.init();

    uart.send("UART echo ready\r\n");

    for (;;) {
        uint8_t byte = uart.receive();
        uart.send(byte);

        if (byte == '\r') {
            uart.send('\n');
        }
    }
}
