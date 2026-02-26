#include <catch2/catch_test_macros.hpp>
#include "ring_buffer.hpp"

// ---------------------------------------------------------------------------
// Basic push / pop
// ---------------------------------------------------------------------------

TEST_CASE("RingBuffer: empty on construction", "[ring_buffer]")
{
    RingBuffer<uint8_t, 8> rb;
    REQUIRE(rb.empty());
    REQUIRE(rb.size() == 0u);
}

TEST_CASE("RingBuffer: push then pop returns same value", "[ring_buffer]")
{
    RingBuffer<uint8_t, 8> rb;
    REQUIRE(rb.push(42u));
    uint8_t val = 0u;
    REQUIRE(rb.pop(val));
    REQUIRE(val == 42u);
    REQUIRE(rb.empty());
}

TEST_CASE("RingBuffer: FIFO order preserved", "[ring_buffer]")
{
    RingBuffer<uint8_t, 8> rb;
    for (uint8_t i = 0u; i < 5u; ++i) REQUIRE(rb.push(i));

    for (uint8_t i = 0u; i < 5u; ++i) {
        uint8_t val = 0xFFu;
        REQUIRE(rb.pop(val));
        REQUIRE(val == i);
    }
    REQUIRE(rb.empty());
}

// ---------------------------------------------------------------------------
// Full / overflow behaviour
// ---------------------------------------------------------------------------

TEST_CASE("RingBuffer: push returns false when full", "[ring_buffer]")
{
    // Capacity is N-1 = 7 usable slots.
    RingBuffer<uint8_t, 8> rb;
    for (int i = 0; i < 7; ++i) REQUIRE(rb.push(static_cast<uint8_t>(i)));
    REQUIRE_FALSE(rb.push(99u));  // 8th push must fail
    REQUIRE(rb.size() == 7u);
}

TEST_CASE("RingBuffer: pop returns false when empty", "[ring_buffer]")
{
    RingBuffer<uint8_t, 8> rb;
    uint8_t val = 0xDEu;
    REQUIRE_FALSE(rb.pop(val));
    REQUIRE(val == 0xDEu);  // output param must not be modified
}

TEST_CASE("RingBuffer: fill then drain then fill again (wrap-around)", "[ring_buffer]")
{
    RingBuffer<uint8_t, 4> rb;  // 3 usable slots

    // Fill
    REQUIRE(rb.push(1u));
    REQUIRE(rb.push(2u));
    REQUIRE(rb.push(3u));
    REQUIRE_FALSE(rb.push(4u));  // full

    // Drain one slot
    uint8_t v;
    REQUIRE(rb.pop(v));
    REQUIRE(v == 1u);

    // Push one more to exercise index wrap-around at the storage boundary
    REQUIRE(rb.push(10u));

    // Drain remaining
    REQUIRE(rb.pop(v)); REQUIRE(v == 2u);
    REQUIRE(rb.pop(v)); REQUIRE(v == 3u);
    REQUIRE(rb.pop(v)); REQUIRE(v == 10u);
    REQUIRE(rb.empty());
}

// ---------------------------------------------------------------------------
// size()
// ---------------------------------------------------------------------------

TEST_CASE("RingBuffer: size tracks element count correctly", "[ring_buffer]")
{
    RingBuffer<uint8_t, 16> rb;
    REQUIRE(rb.size() == 0u);

    rb.push(1u); REQUIRE(rb.size() == 1u);
    rb.push(2u); REQUIRE(rb.size() == 2u);
    rb.push(3u); REQUIRE(rb.size() == 3u);

    uint8_t v;
    rb.pop(v);   REQUIRE(rb.size() == 2u);
    rb.pop(v);   REQUIRE(rb.size() == 1u);
    rb.pop(v);   REQUIRE(rb.size() == 0u);
}

// ---------------------------------------------------------------------------
// Boundary: single-slot buffer (N=2, capacity=1)
// ---------------------------------------------------------------------------

TEST_CASE("RingBuffer: N=2 buffer holds exactly one element", "[ring_buffer]")
{
    RingBuffer<uint8_t, 2> rb;
    REQUIRE(rb.push(0xABu));
    REQUIRE_FALSE(rb.push(0xCDu));  // full after one push
    uint8_t v;
    REQUIRE(rb.pop(v));
    REQUIRE(v == 0xABu);
    REQUIRE(rb.empty());
}

// ---------------------------------------------------------------------------
// Different element types
// ---------------------------------------------------------------------------

TEST_CASE("RingBuffer: works with uint16_t elements", "[ring_buffer]")
{
    RingBuffer<uint16_t, 8> rb;
    REQUIRE(rb.push(0x1234u));
    REQUIRE(rb.push(0x5678u));
    uint16_t v;
    REQUIRE(rb.pop(v)); REQUIRE(v == 0x1234u);
    REQUIRE(rb.pop(v)); REQUIRE(v == 0x5678u);
    REQUIRE(rb.empty());
}

// ---------------------------------------------------------------------------
// empty() transitions
// ---------------------------------------------------------------------------

TEST_CASE("RingBuffer: empty() transitions correctly", "[ring_buffer]")
{
    RingBuffer<uint8_t, 4> rb;
    REQUIRE(rb.empty());

    rb.push(1u);
    REQUIRE_FALSE(rb.empty());

    rb.push(2u);
    rb.push(3u);
    REQUIRE_FALSE(rb.empty());

    uint8_t v;
    rb.pop(v); rb.pop(v); rb.pop(v);
    REQUIRE(rb.empty());
}
