#include "uart.hpp"

// Simple echo demo: every byte received on USART2 is sent straight back.
//
// Connect via the Nucleo's virtual COM port (ST-Link USB, shows up as
// /dev/tty.usbmodem* on macOS or /dev/ttyACM0 on Linux) at 115200 8N1.

int main()
{
    Uart uart;   // 115200 8N1 on USART2 / PA2+PA3
    uart.init();

    uart.send("UART echo ready\r\n");

    for (;;) {
        uint8_t byte = uart.receive();   // block until a character arrives
        uart.send(byte);                 // echo it back

        // Also echo a newline after carriage-return for terminal comfort
        if (byte == '\r') {
            uart.send('\n');
        }
    }
}
