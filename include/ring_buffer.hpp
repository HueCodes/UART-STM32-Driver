#pragma once

#include <cstddef>

/**
 * @file ring_buffer.hpp
 * @brief Lock-free single-producer single-consumer (SPSC) ring buffer.
 *
 * ## ISR / thread-safety model
 * This buffer is safe across one ISR/non-ISR boundary provided the invariant
 * below is maintained:
 *   - Exactly one context calls push()  (the "producer").
 *   - Exactly one context calls pop()   (the "consumer").
 *
 * The @c volatile qualifier on @c head_ and @c tail_ prevents the compiler
 * from caching them in registers across function calls or interrupt returns.
 * On ARMv7-M (Cortex-M4), naturally-aligned 32-bit reads and writes are
 * single-cycle and cannot be torn by an interrupt, so no explicit memory
 * barrier is required for this SPSC pattern.
 *
 * ## Capacity
 * The usable capacity is N-1 elements. One slot is kept empty as a sentinel
 * to distinguish "full" (head+1 == tail) from "empty" (head == tail) without
 * needing a separate count variable.
 *
 * @tparam T Element type.
 * @tparam N Buffer size in elements. Must be a power of 2.
 */
template <typename T, size_t N>
class RingBuffer {
    static_assert((N & (N - 1)) == 0, "RingBuffer size must be a power of 2");
    static constexpr size_t MASK = N - 1;

    volatile size_t head_{0};  ///< Write index; only modified by the producer.
    volatile size_t tail_{0};  ///< Read index;  only modified by the consumer.
    T buf_[N];

public:
    /**
     * @brief Push one value into the buffer (producer side).
     *
     * @param val Value to enqueue.
     * @return true on success; false if the buffer is full (value is dropped).
     * @note Call only from the producer context.
     */
    bool push(T val)
    {
        size_t h    = head_;
        size_t next = (h + 1) & MASK;
        if (next == tail_) return false;  // full
        buf_[h] = val;
        head_   = next;
        return true;
    }

    /**
     * @brief Pop one value from the buffer (consumer side).
     *
     * @param[out] val Written with the dequeued value on success.
     * @return true on success; false if the buffer is empty.
     * @note Call only from the consumer context.
     */
    bool pop(T& val)
    {
        size_t t = tail_;
        if (t == head_) return false;  // empty
        val   = buf_[t];
        tail_ = (t + 1) & MASK;
        return true;
    }

    /**
     * @brief Return true if the buffer contains no elements.
     */
    bool empty() const { return head_ == tail_; }

    /**
     * @brief Return the number of elements currently stored.
     * @return Value in [0, N-1].
     */
    size_t size() const { return (head_ - tail_) & MASK; }
};
