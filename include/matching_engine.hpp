#pragma once

#include "types.hpp"
#include "book.hpp"
#include "order_pool.hpp"
#include "spsc_ring_buffer.hpp"
#include <vector>
#include <chrono>

namespace OrderBook {

/**
 * Single-threaded matching engine that owns all order book data structures.
 * Processes commands from the lock-free ring buffer sequentially.
 * 
 * Key design principle: Single writer eliminates need for locks on the order book,
 * maximizing performance on the critical path.
 */
class MatchingEngine {
private:
    Book book_;
    OrderPool order_pool_;
    SPSCRingBuffer* ring_buffer_;
    
    // Order tracking for cancellations - maps order_id to Order*
    std::vector<Order*> order_map_;
    
    // Statistics
    std::vector<long long> trade_latencies_ns_;
    uint64_t orders_processed_;
    uint64_t trades_executed_;
    uint64_t orders_rejected_;
    uint64_t total_buy_quantity_matched_;
    uint64_t total_sell_quantity_matched_;
    
    void handle_new_order(const Command& cmd, const std::chrono::high_resolution_clock::time_point& processing_start) noexcept;
    void handle_cancel_order(uint64_t order_id) noexcept;
    void match_order(Order* aggressor, const std::chrono::high_resolution_clock::time_point& processing_start) noexcept;
    void match_against_asks(Order* buy_order, const std::chrono::high_resolution_clock::time_point& processing_start) noexcept;
    void match_against_bids(Order* sell_order, const std::chrono::high_resolution_clock::time_point& processing_start) noexcept;
    void execute_trade(uint64_t aggressor_id, uint64_t resting_id, int64_t price, 
                      uint64_t quantity, const std::chrono::high_resolution_clock::time_point& processing_start) noexcept;
    
public:
    explicit MatchingEngine(SPSCRingBuffer* ring_buffer);
    
    /**
     * Main processing loop - runs on consumer thread
     * Continuously dequeues commands and processes them
     */
    void run() noexcept;
    
    // Getters for statistics
    uint64_t orders_processed() const noexcept;
    uint64_t trades_executed() const noexcept;
    uint64_t orders_rejected() const noexcept;
    const std::vector<long long>& trade_latencies() const noexcept;
    uint64_t total_buy_quantity_matched() const noexcept;
    uint64_t total_sell_quantity_matched() const noexcept;
};

} // namespace OrderBook