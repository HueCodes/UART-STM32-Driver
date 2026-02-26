#include "uart.hpp"
#include "rcc.hpp"

// Simple interactive demo for the UART driver on a Nucleo-F401RE.
//
// Connect via the Nucleo's ST-Link virtual COM port (/dev/tty.usbmodem* on
// macOS, /dev/ttyACM0 on Linux) at 115200 8N1.
//
// What to observe on a logic analyser / scope:
//   - PA2 (TX): UART frames at 115200 baud, 8N1 (one start bit, 8 data bits,
//               no parity, one stop bit). Idle state is HIGH.
//   - PA3 (RX): Frames from the host in the same format.
//   - The TXE interrupt fires per byte and is disabled between bursts
//     (CR1.TXEIE goes LOW when the TX ring buffer drains).

// Number of lines received between stats prints.
static constexpr uint32_t STATS_INTERVAL = 100u;

int main()
{
    // -----------------------------------------------------------------------
    // 1. Clock setup: HSE -> PLL -> 84 MHz SYSCLK. Halt on failure.
    //    Production firmware would fall back to the 16 MHz HSI instead of
    //    halting, but a hard stop here makes the failure visible on a debugger.
    // -----------------------------------------------------------------------
    if (!rcc_init()) {
        for (;;) {}  // HSE or PLL did not lock; inspect RCC->CR in debugger
    }

    // -----------------------------------------------------------------------
    // 2. UART init: USART2 at 115200 8N1 on PA2/PA3.
    //    Also configures SysTick for 1 ms timeouts used by receive_line().
    // -----------------------------------------------------------------------
    Uart& uart = uart_instance();
    if (!uart.init()) {
        for (;;) {}  // init() should not fail for USART2; check peripheral map
    }

    uart.send("UART driver ready. Send lines terminated with \\n.\r\n");

    // -----------------------------------------------------------------------
    // 3. Echo loop using receive_line() to demonstrate line-oriented receive.
    //    Every STATS_INTERVAL lines the driver statistics are printed.
    // -----------------------------------------------------------------------
    char     line[128];
    uint32_t line_count = 0u;

    for (;;) {
        bool got_line = uart.receive_line(line, sizeof(line), 10000u);

        if (got_line) {
            // Echo the line back with a prefix.
            uart.send("> ");
            uart.send(line);
            uart.send("\r\n");
            ++line_count;
        } else {
            // Timeout: prompt the user and continue.
            uart.send("[timeout waiting for line]\r\n");
        }

        // Print statistics every STATS_INTERVAL lines.
        if (line_count > 0u && (line_count % STATS_INTERVAL) == 0u) {
            Uart::Stats s = uart.get_stats();

            // Format stats without printf to avoid pulling in large newlib code.
            // In a real project, printf via syscalls.cpp routes through here.
            uart.send("--- stats ---\r\n");

            // A simple decimal formatter for uint32 fields.
            auto print_field = [&](const char* label, uint32_t val) {
                uart.send(label);
                char tmp[12];
                int  idx = 11;
                tmp[idx] = '\0';
                if (val == 0u) { tmp[--idx] = '0'; }
                while (val > 0u) { tmp[--idx] = static_cast<char>('0' + val % 10u); val /= 10u; }
                uart.send(&tmp[idx]);
                uart.send("\r\n");
            };

            print_field("  bytes_sent:          ", s.bytes_sent);
            print_field("  bytes_received:      ", s.bytes_received);
            print_field("  tx_buffer_overflows: ", s.tx_buffer_overflows);
            print_field("  rx_buffer_overflows: ", s.rx_buffer_overflows);
            print_field("  overrun_errors:      ", s.overrun_errors);
            print_field("  framing_errors:      ", s.framing_errors);
            print_field("  noise_errors:        ", s.noise_errors);
        }
    }
}
