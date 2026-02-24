#pragma once

#include <cstddef>

// Single-producer single-consumer ring buffer.
// N must be a power of 2; size mask replaces modulo.
// head_ and tail_ are volatile for safe ISR/non-ISR sharing.
template <typename T, size_t N>
class RingBuffer {
    static_assert((N & (N - 1)) == 0, "RingBuffer size must be a power of 2");
    static constexpr size_t MASK = N - 1;

    volatile size_t head_{0};
    volatile size_t tail_{0};
    T buf_[N];

public:
    // Returns false if full; byte is dropped.
    bool push(T val)
    {
        size_t h = head_;
        size_t next = (h + 1) & MASK;
        if (next == tail_) return false;
        buf_[h] = val;
        head_ = next;
        return true;
    }

    // Returns false if empty.
    bool pop(T& val)
    {
        size_t t = tail_;
        if (t == head_) return false;
        val = buf_[t];
        tail_ = (t + 1) & MASK;
        return true;
    }

    bool empty() const
    {
        return head_ == tail_;
    }
};
