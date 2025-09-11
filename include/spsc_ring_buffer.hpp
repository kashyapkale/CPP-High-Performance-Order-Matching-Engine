#pragma once

#include "types.hpp"
#include <atomic>
#include <vector>

namespace OrderBook {

/**
 * Single-Producer Single-Consumer Lock-Free Ring Buffer
 * 
 * Critical design choices for ultra-low latency:
 * 1. Power-of-2 size allows bitwise masking instead of modulo operation
 * 2. Separate cache lines for head/tail to avoid false sharing
 * 3. Acquire-Release memory ordering provides necessary synchronization without seq_cst overhead
 * 4. Producer and consumer each own their respective indices to minimize contention
 */
class SPSCRingBuffer {
private:
    alignas(64) std::atomic<uint64_t> head_;  // Producer writes here, separate cache line
    alignas(64) std::atomic<uint64_t> tail_;  // Consumer reads from here, separate cache line
    std::vector<Command> buffer_;
    
public:
    SPSCRingBuffer();
    
    /**
     * Producer enqueue operation
     * Uses release memory ordering to ensure all writes to the command
     * are visible to the consumer before the head index is updated
     */
    bool enqueue(const Command& cmd) noexcept;
    
    /**
     * Consumer dequeue operation  
     * Uses acquire memory ordering to ensure all writes from producer
     * are visible before reading the command data
     */
    bool dequeue(Command& cmd) noexcept;
};

} // namespace OrderBook