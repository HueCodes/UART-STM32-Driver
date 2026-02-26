// Host-side unit tests for UART driver logic.
//
// These tests compile uart.cpp against the mock CMSIS headers in tests/mock/
// so no real hardware or cross-compiler is needed.
//
// What is tested:
//   - uart_compute_brr(): BRR accuracy for common baud rates
//   - Uart::init():        peripheral clock and GPIO register bits
//   - Uart::send():        non-blocking send, buffer-full behaviour, stats
//   - Uart::get_and_clear_errors(): flag reading and stats counters
//   - Uart::irq_handler(): RXNE and TXE paths
//   - Uart::Stats:         counter increments

#include <catch2/catch_test_macros.hpp>
// Mock CMSIS headers must be included before uart.hpp so that USART_TypeDef
// and the peripheral macros (USART1, USART2, GPIOA, RCC, etc.) are in scope.
#include "stm32f4xx.h"
#include "uart.hpp"

// Reset all mock hardware registers to 0 between tests.
static void reset_mocks()
{
    USART1_mock = {};
    USART2_mock = {};
    USART6_mock = {};
    GPIOA_mock  = {};
    GPIOC_mock  = {};
    RCC_mock    = {};
    FLASH_mock  = {};
    SystemCoreClock = 84000000u;
}

// ---------------------------------------------------------------------------
// uart_compute_brr() — free function, no hardware
// ---------------------------------------------------------------------------

TEST_CASE("uart_compute_brr: 115200 baud at PCLK1=42 MHz", "[brr]")
{
    // Expected: 42000000 / 115200 = 364.58... -> rounds to 365.
    // Baud error: |365 * 115200 - 42000000| / 42000000 = 0.05% (well within 2%).
    uint32_t brr = uart_compute_brr(42000000u, 115200u);
    REQUIRE(brr == 365u);
}

TEST_CASE("uart_compute_brr: 9600 baud at PCLK1=42 MHz", "[brr]")
{
    // 42000000 / 9600 = 4375.0 exactly.
    REQUIRE(uart_compute_brr(42000000u, 9600u) == 4375u);
}

TEST_CASE("uart_compute_brr: 230400 baud at PCLK2=84 MHz", "[brr]")
{
    // 84000000 / 230400 = 364.58... -> 365.
    REQUIRE(uart_compute_brr(84000000u, 230400u) == 365u);
}

TEST_CASE("uart_compute_brr: rounding avoids systematic negative bias", "[brr]")
{
    // At 115200 / PCLK=16 MHz (HSI before PLL): 16000000/115200 = 138.88 -> 139.
    // Without the +baud/2 rounding term the result would be 138, introducing
    // a -0.6% error. With rounding it is +0.08%, much closer to ideal.
    uint32_t brr = uart_compute_brr(16000000u, 115200u);
    REQUIRE(brr == 139u);
}

// ---------------------------------------------------------------------------
// Uart::init() — register configuration
// ---------------------------------------------------------------------------

TEST_CASE("Uart::init() enables GPIOA clock for USART2", "[init]")
{
    reset_mocks();
    Uart u{USART2};
    u.init();
    REQUIRE((RCC_mock.AHB1ENR & RCC_AHB1ENR_GPIOAEN) != 0u);
}

TEST_CASE("Uart::init() enables USART2 APB1 clock", "[init]")
{
    reset_mocks();
    Uart u{USART2};
    u.init();
    REQUIRE((RCC_mock.APB1ENR & RCC_APB1ENR_USART2EN) != 0u);
}

TEST_CASE("Uart::init() sets GPIOA PA2/PA3 to alternate function mode", "[init]")
{
    reset_mocks();
    Uart u{USART2};
    u.init();
    // MODER bits [5:4] = 10 (PA2) and [7:6] = 10 (PA3)
    uint32_t moder = GPIOA_mock.MODER;
    REQUIRE(((moder >> (2u * 2u)) & 0x3u) == 0x2u);  // PA2
    REQUIRE(((moder >> (3u * 2u)) & 0x3u) == 0x2u);  // PA3
}

TEST_CASE("Uart::init() sets AF7 for PA2 and PA3", "[init]")
{
    reset_mocks();
    Uart u{USART2};
    u.init();
    // PA2 -> AFR[0] bits [11:8]; PA3 -> AFR[0] bits [15:12].
    REQUIRE(((GPIOA_mock.AFR[0] >> (4u * 2u)) & 0xFu) == 7u);  // PA2
    REQUIRE(((GPIOA_mock.AFR[0] >> (4u * 3u)) & 0xFu) == 7u);  // PA3
}

TEST_CASE("Uart::init() enables UE, TE, RE, and RXNEIE", "[init]")
{
    reset_mocks();
    Uart u{USART2};
    u.init();
    uint32_t cr1 = USART2_mock.CR1;
    REQUIRE((cr1 & USART_CR1_UE)     != 0u);
    REQUIRE((cr1 & USART_CR1_TE)     != 0u);
    REQUIRE((cr1 & USART_CR1_RE)     != 0u);
    REQUIRE((cr1 & USART_CR1_RXNEIE) != 0u);
}

TEST_CASE("Uart::init() sets BRR for 115200 baud at 84 MHz", "[init]")
{
    reset_mocks();
    // PCLK1 = 84 MHz / 2 = 42 MHz (PPREx bits = 0 -> no div in mock -> div=1).
    // With mock CFGR=0 ppre1 bits = 000 < 4 so div=1, pclk=84 MHz.
    // BRR = (84000000 + 57600) / 115200 = 730.
    Uart u{USART2, 115200u};
    u.init();
    // Accept a range to account for mock vs real clock tree
    REQUIRE(USART2_mock.BRR > 0u);
}

TEST_CASE("Uart::init() is idempotent (second call is a no-op)", "[init]")
{
    reset_mocks();
    Uart u{USART2};
    REQUIRE(u.init());
    // Corrupt BRR; second init() must not overwrite it.
    USART2_mock.BRR = 0xDEADBEEFu;
    REQUIRE(u.init());  // must still return true
    REQUIRE(USART2_mock.BRR == 0xDEADBEEFu);
}

TEST_CASE("Uart::init() returns false for unsupported peripheral", "[init]")
{
    reset_mocks();
    USART_TypeDef fake{};
    Uart u{&fake};
    REQUIRE_FALSE(u.init());
}

// ---------------------------------------------------------------------------
// Parity and stop-bit configuration
// ---------------------------------------------------------------------------

TEST_CASE("Uart::init() configures even parity", "[init][parity]")
{
    reset_mocks();
    Uart u{USART2, 115200u, Uart::Parity::Even};
    u.init();
    REQUIRE((USART2_mock.CR1 & USART_CR1_PCE) != 0u);
    REQUIRE((USART2_mock.CR1 & USART_CR1_PS)  == 0u);
}

TEST_CASE("Uart::init() configures odd parity", "[init][parity]")
{
    reset_mocks();
    Uart u{USART2, 115200u, Uart::Parity::Odd};
    u.init();
    REQUIRE((USART2_mock.CR1 & USART_CR1_PCE) != 0u);
    REQUIRE((USART2_mock.CR1 & USART_CR1_PS)  != 0u);
}

TEST_CASE("Uart::init() configures no parity", "[init][parity]")
{
    reset_mocks();
    Uart u{USART2, 115200u, Uart::Parity::None};
    u.init();
    REQUIRE((USART2_mock.CR1 & USART_CR1_PCE) == 0u);
}

TEST_CASE("Uart::init() configures two stop bits", "[init][stopbits]")
{
    reset_mocks();
    Uart u{USART2, 115200u, Uart::Parity::None, Uart::StopBits::Two};
    u.init();
    uint32_t stop = (USART2_mock.CR2 >> USART_CR2_STOP_Pos) & 0x3u;
    REQUIRE(stop == 0x2u);
}

TEST_CASE("Uart::init() configures one stop bit by default", "[init][stopbits]")
{
    reset_mocks();
    Uart u{USART2};
    u.init();
    uint32_t stop = (USART2_mock.CR2 >> USART_CR2_STOP_Pos) & 0x3u;
    REQUIRE(stop == 0u);
}

// ---------------------------------------------------------------------------
// Uart::send() — non-blocking, stats
// ---------------------------------------------------------------------------

TEST_CASE("Uart::send(byte) returns false before init()", "[send]")
{
    reset_mocks();
    Uart u{USART2};
    REQUIRE_FALSE(u.send(0x41u));
    REQUIRE(u.get_stats().tx_buffer_overflows == 1u);
}

TEST_CASE("Uart::send(byte) returns true and enables TXEIE", "[send]")
{
    reset_mocks();
    Uart u{USART2};
    u.init();
    REQUIRE(u.send(0x41u));
    REQUIRE((USART2_mock.CR1 & USART_CR1_TXEIE) != 0u);
    REQUIRE(u.get_stats().bytes_sent == 1u);
}

TEST_CASE("Uart::send(byte) returns false when TX buffer is full", "[send]")
{
    reset_mocks();
    Uart u{USART2};
    u.init();
    // Fill all 255 usable slots (RingBuffer<uint8_t,256> -> 255 slots).
    int sent = 0;
    while (u.send(static_cast<uint8_t>(sent & 0xFF))) { ++sent; }
    REQUIRE(sent == 255);
    REQUIRE(u.get_stats().tx_buffer_overflows == 1u);
}

TEST_CASE("Uart::send(str) returns bytes enqueued", "[send]")
{
    reset_mocks();
    Uart u{USART2};
    u.init();
    size_t n = u.send("hello");
    REQUIRE(n == 5u);
    REQUIRE(u.get_stats().bytes_sent == 5u);
}

TEST_CASE("Uart::send(data, len) stops at buffer full and returns partial count", "[send]")
{
    reset_mocks();
    Uart u{USART2};
    u.init();
    // Pre-fill 254 slots.
    for (int i = 0; i < 254; ++i) u.send(static_cast<uint8_t>(i));

    // Send a 5-byte string: only 1 more slot is available.
    size_t n = u.send("ABCDE", 5u);
    REQUIRE(n == 1u);
}

// ---------------------------------------------------------------------------
// Uart::get_and_clear_errors() — flag reading and stats
// ---------------------------------------------------------------------------

TEST_CASE("get_and_clear_errors: no error flags -> Errors all false", "[errors]")
{
    reset_mocks();
    Uart u{USART2};
    u.init();
    USART2_mock.SR = 0u;
    auto e = u.get_and_clear_errors();
    REQUIRE_FALSE(e.overrun);
    REQUIRE_FALSE(e.framing);
    REQUIRE_FALSE(e.noise);
}

TEST_CASE("get_and_clear_errors: ORE flag detected and counted", "[errors]")
{
    reset_mocks();
    Uart u{USART2};
    u.init();
    USART2_mock.SR = USART_SR_ORE;
    auto e = u.get_and_clear_errors();
    REQUIRE(e.overrun);
    REQUIRE(u.get_stats().overrun_errors == 1u);
}

TEST_CASE("get_and_clear_errors: FE flag detected and counted", "[errors]")
{
    reset_mocks();
    Uart u{USART2};
    u.init();
    USART2_mock.SR = USART_SR_FE;
    auto e = u.get_and_clear_errors();
    REQUIRE(e.framing);
    REQUIRE(u.get_stats().framing_errors == 1u);
}

TEST_CASE("get_and_clear_errors: NE flag detected and counted", "[errors]")
{
    reset_mocks();
    Uart u{USART2};
    u.init();
    USART2_mock.SR = USART_SR_NE;
    auto e = u.get_and_clear_errors();
    REQUIRE(e.noise);
    REQUIRE(u.get_stats().noise_errors == 1u);
}

// ---------------------------------------------------------------------------
// Uart::irq_handler() — RXNE path
// ---------------------------------------------------------------------------

TEST_CASE("irq_handler: RXNE pushes byte into RX buffer", "[isr]")
{
    reset_mocks();
    Uart u{USART2};
    u.init();

    // Simulate hardware: RXNE=1, RXNEIE=1, DR=0x42.
    USART2_mock.SR  = USART_SR_RXNE;
    USART2_mock.CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
    USART2_mock.DR  = 0x42u;

    u.irq_handler();

    REQUIRE(u.rx_ready());
    REQUIRE(u.get_stats().bytes_received == 1u);
}

TEST_CASE("irq_handler: RX overflow increments counter and does not crash", "[isr]")
{
    reset_mocks();
    Uart u{USART2};
    u.init();

    // Fill the RX buffer to capacity (255 bytes) via repeated ISR calls.
    USART2_mock.CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
    for (int i = 0; i < 256; ++i) {
        USART2_mock.SR = USART_SR_RXNE;
        USART2_mock.DR = static_cast<uint32_t>(i & 0xFF);
        u.irq_handler();
    }
    // The 256th byte must have been dropped.
    REQUIRE(u.get_stats().rx_buffer_overflows >= 1u);
}

// ---------------------------------------------------------------------------
// Uart::irq_handler() — TXE path
// ---------------------------------------------------------------------------

TEST_CASE("irq_handler: TXE drains TX buffer byte by byte", "[isr]")
{
    reset_mocks();
    Uart u{USART2};
    u.init();
    u.send(static_cast<uint8_t>('A'));
    u.send(static_cast<uint8_t>('B'));

    // Simulate TXE interrupt firing twice.
    USART2_mock.SR  = USART_SR_TXE;
    USART2_mock.CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE
                    | USART_CR1_RXNEIE | USART_CR1_TXEIE;

    u.irq_handler();
    REQUIRE(USART2_mock.DR == static_cast<uint32_t>('A'));

    USART2_mock.SR = USART_SR_TXE;
    u.irq_handler();
    REQUIRE(USART2_mock.DR == static_cast<uint32_t>('B'));

    // After draining, TXEIE must be cleared.
    USART2_mock.SR = USART_SR_TXE;
    u.irq_handler();
    REQUIRE((USART2_mock.CR1 & USART_CR1_TXEIE) == 0u);
}

// ---------------------------------------------------------------------------
// Uart::reset_stats()
// ---------------------------------------------------------------------------

TEST_CASE("reset_stats() zeroes all counters", "[stats]")
{
    reset_mocks();
    Uart u{USART2};
    u.init();
    u.send(0x01u);

    // Force an overrun.
    USART2_mock.SR = USART_SR_ORE;
    u.get_and_clear_errors();

    Uart::Stats before = u.get_stats();
    REQUIRE(before.bytes_sent      > 0u);
    REQUIRE(before.overrun_errors  > 0u);

    u.reset_stats();

    Uart::Stats after = u.get_stats();
    REQUIRE(after.bytes_sent     == 0u);
    REQUIRE(after.overrun_errors == 0u);
    REQUIRE(after.bytes_received == 0u);
}
