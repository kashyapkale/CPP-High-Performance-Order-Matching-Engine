#include "spsc_ring_buffer.hpp"

namespace OrderBook {

SPSCRingBuffer::SPSCRingBuffer() 
    : head_(0), tail_(0), buffer_(RING_BUFFER_SIZE) {}

bool SPSCRingBuffer::enqueue(const Command& cmd) noexcept {
    const uint64_t current_head = head_.load(std::memory_order_relaxed);
    const uint64_t next_head = (current_head + 1) & RING_BUFFER_MASK;
    
    // Check if buffer is full
    if (next_head == tail_.load(std::memory_order_acquire)) {
        return false;
    }
    
    buffer_[current_head] = cmd;
    head_.store(next_head, std::memory_order_release);
    return true;
}

bool SPSCRingBuffer::dequeue(Command& cmd) noexcept {
    const uint64_t current_tail = tail_.load(std::memory_order_relaxed);
    
    // Check if buffer is empty
    if (current_tail == head_.load(std::memory_order_acquire)) {
        return false;
    }
    
    cmd = buffer_[current_tail];
    tail_.store((current_tail + 1) & RING_BUFFER_MASK, std::memory_order_release);
    return true;
}

} // namespace OrderBook