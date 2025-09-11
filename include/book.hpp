#pragma once

#include "types.hpp"
#include <vector>

namespace OrderBook {

/**
 * Order Book implementation using direct-mapped price grid for O(1) lookup.
 * 
 * Instead of std::map<price, PriceLevel> which has O(log n) lookup,
 * we use std::vector<PriceLevel> with direct indexing for O(1) access.
 * This assumes a bounded price range but provides significant performance benefits.
 */
class Book {
private:
    std::vector<PriceLevel> bid_levels_;    // Index = price, higher indices = higher prices
    std::vector<PriceLevel> ask_levels_;    // Index = price, lower indices = lower prices
    int64_t best_bid_price_;
    int64_t best_ask_price_;
    
    void update_best_bid() noexcept;
    void update_best_ask() noexcept;
    
public:
    Book();
    
    /**
     * Add order to appropriate price level and side
     * Updates best bid/ask tracking for O(1) top-of-book access
     */
    void add_order(Order* order) noexcept;
    
    /**
     * Remove order from book (used for cancellations)
     * Updates best bid/ask if necessary
     */
    void remove_order(Order* order) noexcept;
    
    /**
     * Get price level for specific price and side
     * O(1) direct array access
     */
    PriceLevel* get_price_level(int64_t price, Side side) noexcept;
    
    int64_t best_bid() const noexcept;
    int64_t best_ask() const noexcept;
};

} // namespace OrderBook