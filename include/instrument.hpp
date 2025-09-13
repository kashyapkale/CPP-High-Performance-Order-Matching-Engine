#pragma once

#include "types.hpp"
#include <string>
#include <cstdint>

namespace OrderBook {

/**
 * Instrument identifier and configuration
 */
struct Instrument {
    uint32_t instrument_id;
    std::string symbol;
    int64_t tick_size;          // Minimum price increment
    uint64_t lot_size;          // Minimum quantity increment
    int64_t price_min;          // Minimum allowed price
    int64_t price_max;          // Maximum allowed price
    uint64_t max_order_size;    // Maximum single order quantity
    
    Instrument(uint32_t id, const std::string& sym, int64_t tick = 1, 
               uint64_t lot = 1, int64_t p_min = 0, int64_t p_max = 10000,
               uint64_t max_size = 1000000)
        : instrument_id(id), symbol(sym), tick_size(tick), lot_size(lot),
          price_min(p_min), price_max(p_max), max_order_size(max_size) {}
    
    bool is_valid_price(int64_t price) const noexcept {
        return price >= price_min && 
               price <= price_max && 
               (price % tick_size) == 0;
    }
    
    bool is_valid_quantity(uint64_t quantity) const noexcept {
        return quantity > 0 && 
               quantity <= max_order_size && 
               (quantity % lot_size) == 0;
    }
};

} // namespace OrderBook