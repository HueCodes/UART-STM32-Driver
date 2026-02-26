#include "uart.hpp"

// Route newlib stdout / stderr through the USART2 singleton.
// Called by printf, puts, fwrite, etc. when -specs=nano.specs is active.
//
// _write() is required to return the number of bytes "written" to satisfy
// newlib's contract. We return len unconditionally even if the TX buffer fills
// up, so that printf does not retry in a loop. The caller can inspect
// uart_instance().get_stats().tx_buffer_overflows to detect dropped output.
extern "C" int _write(int fd, const char* buf, int len)
{
    if (fd == 1 || fd == 2) {
        uart_instance().send(buf, static_cast<size_t>(len));
    }
    return len;
}
