#pragma once

#include "spsc_ring_buffer.hpp"

namespace OrderBook {

/**
 * Feed handler simulates realistic market activity
 * Generates orders with appropriate distribution:
 * - 50% passive orders (near current bid/ask)
 * - 20% aggressive orders (cross the spread) 
 * - 30% cancellation requests
 */
class FeedHandler {
public:
    static void run(SPSCRingBuffer* ring_buffer) noexcept;
};

} // namespace OrderBook