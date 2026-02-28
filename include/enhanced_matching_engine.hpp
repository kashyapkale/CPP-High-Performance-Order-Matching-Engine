#pragma once

#include "types.hpp"
#include "book.hpp"
#include "order_pool.hpp"
#include "spsc_ring_buffer.hpp"
#include "market_data.hpp"
#include <vector>
#include <chrono>
#include <memory>

namespace OrderBook {

/**
 * Enhanced matching engine with support for IOC/FOK orders
 * and market data publishing
 */
class EnhancedMatchingEngine {
private:
    Book book_;
    OrderPool order_pool_;
    SPSCRingBuffer* ring_buffer_;
    std::unique_ptr<MarketDataManager> market_data_manager_;
    
    // Order tracking for cancellations
    std::vector<Order*> order_map_;
    
    // Enhanced statistics
    struct OrderTypeStats {
        uint64_t submitted = 0;
        uint64_t filled = 0;
        uint64_t partial_fills = 0;
        uint64_t cancelled = 0;
        uint64_t rejected = 0;
    };
    
    std::array<OrderTypeStats, 3> order_type_stats_; // LIMIT, IOC, FOK
    std::vector<long long> trade_latencies_ns_;
    uint64_t orders_processed_;
    uint64_t trades_executed_;
    uint64_t orders_rejected_;
    uint64_t total_buy_quantity_matched_;
    uint64_t total_sell_quantity_matched_;
    
public:
    explicit EnhancedMatchingEngine(SPSCRingBuffer* ring_buffer);
    
    /**
     * Set market data manager for publishing L2 data and trades
     */
    void set_market_data_manager(std::unique_ptr<MarketDataManager> manager);
    
    /**
     * Main processing loop with enhanced order type support
     */
    void run() noexcept;
    
    // Statistics getters
    uint64_t orders_processed() const noexcept;
    uint64_t trades_executed() const noexcept;
    uint64_t orders_rejected() const noexcept;
    const std::vector<long long>& trade_latencies() const noexcept;
    uint64_t total_buy_quantity_matched() const noexcept;
    uint64_t total_sell_quantity_matched() const noexcept;
    
    // Order type statistics
    const OrderTypeStats& get_order_type_stats(OrderType type) const noexcept;
    void print_order_type_statistics() const noexcept;
    
    // Market data
    Level2Snapshot create_level2_snapshot() const noexcept;
    
private:
    void handle_new_order(const Command& cmd, const std::chrono::high_resolution_clock::time_point& processing_start) noexcept;
    void handle_cancel_order(uint64_t order_id) noexcept;
    
    // Enhanced matching with order type support
    enum class MatchResult {
        FULLY_MATCHED,
        PARTIALLY_MATCHED, 
        NO_MATCH,
        REJECTED
    };
    
    MatchResult match_order(Order* order, const std::chrono::high_resolution_clock::time_point& processing_start) noexcept;
    MatchResult match_limit_order(Order* order, const std::chrono::high_resolution_clock::time_point& processing_start) noexcept;
    MatchResult match_ioc_order(Order* order, const std::chrono::high_resolution_clock::time_point& processing_start) noexcept;
    MatchResult match_fok_order(Order* order, const std::chrono::high_resolution_clock::time_point& processing_start) noexcept;
    
    void match_against_asks(Order* buy_order, const std::chrono::high_resolution_clock::time_point& processing_start) noexcept;
    void match_against_bids(Order* sell_order, const std::chrono::high_resolution_clock::time_point& processing_start) noexcept;
    
    // FOK order validation - check if order can be fully filled
    bool can_fill_completely(Order* order) const noexcept;
    uint64_t calculate_fillable_quantity(Order* order) const noexcept;
    
    void execute_trade(uint64_t aggressor_id, uint64_t resting_id, int64_t price, 
                      uint64_t quantity, const std::chrono::high_resolution_clock::time_point& processing_start) noexcept;
    
    void update_order_status(Order* order) noexcept;
    void publish_market_data_update(Side side, int64_t price) noexcept;
    void reject_order(Order* order, const std::string& reason) noexcept;
};

} // namespace OrderBook