#pragma once

#include "types.hpp"
#include "instrument.hpp"
#include "book.hpp"
#include "order_pool.hpp"
#include "spsc_ring_buffer.hpp"
#include <unordered_map>
#include <memory>
#include <vector>

namespace OrderBook {

/**
 * Enhanced command structure with instrument support
 */
struct MultiInstrumentCommand {
    CommandType type;
    uint32_t instrument_id;
    uint64_t order_id;
    Side side;
    int64_t price;
    uint64_t quantity;
    std::chrono::high_resolution_clock::time_point producer_timestamp;
    
    MultiInstrumentCommand() noexcept = default;
};

/**
 * Multi-instrument matching engine that manages separate order books
 * for different instruments while sharing the same order pool
 */
class MultiInstrumentEngine {
private:
    std::unordered_map<uint32_t, std::unique_ptr<Book>> books_;
    std::unordered_map<uint32_t, Instrument> instruments_;
    std::unique_ptr<OrderPool> order_pool_;
    SPSCRingBuffer* ring_buffer_;
    
    // Enhanced order tracking with instrument mapping
    std::vector<std::pair<Order*, uint32_t>> order_map_;  // Order* -> instrument_id
    
    // Per-instrument statistics
    std::unordered_map<uint32_t, uint64_t> trades_per_instrument_;
    std::unordered_map<uint32_t, uint64_t> volume_per_instrument_;
    
    // Global statistics
    std::vector<long long> trade_latencies_ns_;
    uint64_t orders_processed_;
    uint64_t total_trades_executed_;
    
public:
    explicit MultiInstrumentEngine(SPSCRingBuffer* ring_buffer);
    
    /**
     * Add a new instrument to the engine
     */
    bool add_instrument(const Instrument& instrument);
    
    /**
     * Remove an instrument (after processing all pending orders)
     */
    bool remove_instrument(uint32_t instrument_id);
    
    /**
     * Main processing loop for multi-instrument orders
     */
    void run() noexcept;
    
    /**
     * Get order book for specific instrument
     */
    const Book* get_book(uint32_t instrument_id) const noexcept;
    
    // Statistics getters
    uint64_t orders_processed() const noexcept;
    uint64_t total_trades_executed() const noexcept;
    uint64_t trades_for_instrument(uint32_t instrument_id) const noexcept;
    uint64_t volume_for_instrument(uint32_t instrument_id) const noexcept;
    const std::vector<long long>& trade_latencies() const noexcept;
    
private:
    void handle_new_order(const MultiInstrumentCommand& cmd, 
                         const std::chrono::high_resolution_clock::time_point& processing_start) noexcept;
    void handle_cancel_order(uint32_t instrument_id, uint64_t order_id) noexcept;
    bool validate_order(const MultiInstrumentCommand& cmd) const noexcept;
    void execute_trade(uint32_t instrument_id, uint64_t aggressor_id, uint64_t resting_id, 
                      int64_t price, uint64_t quantity,
                      const std::chrono::high_resolution_clock::time_point& processing_start) noexcept;
    void match_order(uint32_t instrument_id, Order* order, 
                    const std::chrono::high_resolution_clock::time_point& processing_start) noexcept;
};

/**
 * SPSC Ring Buffer for multi-instrument commands
 */
class MultiInstrumentRingBuffer {
private:
    alignas(64) std::atomic<uint64_t> head_;
    alignas(64) std::atomic<uint64_t> tail_;
    std::vector<MultiInstrumentCommand> buffer_;
    
public:
    MultiInstrumentRingBuffer();
    bool enqueue(const MultiInstrumentCommand& cmd) noexcept;
    bool dequeue(MultiInstrumentCommand& cmd) noexcept;
};

} // namespace OrderBook