#include "uart.hpp"

// Route newlib stdout/stderr through the UART singleton.
// Called by printf, puts, etc. when -specs=nano.specs is active.
extern "C" int _write(int fd, const char* buf, int len)
{
    if (fd == 1 || fd == 2) {
        uart_instance().send(buf, static_cast<size_t>(len));
    }
    return len;
}
