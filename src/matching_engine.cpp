#include "matching_engine.hpp"
#include <iostream>
#include <algorithm>

namespace OrderBook {

MatchingEngine::MatchingEngine(SPSCRingBuffer* ring_buffer) 
    : order_pool_(MAX_ORDERS), ring_buffer_(ring_buffer), 
      order_map_(MAX_ORDERS, nullptr), orders_processed_(0),
      trades_executed_(0), orders_rejected_(0),
      total_buy_quantity_matched_(0),
      total_sell_quantity_matched_(0) {
    
    trade_latencies_ns_.reserve(TOTAL_ORDERS_TO_GENERATE / 10);  // Estimate 10% will trade
}

void MatchingEngine::run() noexcept {
    Command cmd;
    
    while (orders_processed_ < TOTAL_ORDERS_TO_GENERATE) {
        if (ring_buffer_->dequeue(cmd)) {
            const auto processing_start = std::chrono::high_resolution_clock::now();
            
            if (cmd.type == CommandType::NEW) {
                handle_new_order(cmd, processing_start);
            } else {
                handle_cancel_order(cmd.order_id);
            }
            
            ++orders_processed_;
        }
        // Tight loop for minimum latency - no yield or sleep
    }
}

uint64_t MatchingEngine::orders_processed() const noexcept { 
    return orders_processed_; 
}

uint64_t MatchingEngine::trades_executed() const noexcept {
    return trades_executed_;
}

uint64_t MatchingEngine::orders_rejected() const noexcept {
    return orders_rejected_;
}

const std::vector<long long>& MatchingEngine::trade_latencies() const noexcept { 
    return trade_latencies_ns_; 
}

uint64_t MatchingEngine::total_buy_quantity_matched() const noexcept { 
    return total_buy_quantity_matched_; 
}

uint64_t MatchingEngine::total_sell_quantity_matched() const noexcept { 
    return total_sell_quantity_matched_; 
}

void MatchingEngine::handle_new_order(const Command& cmd, const std::chrono::high_resolution_clock::time_point& processing_start) noexcept {
    Order* order = order_pool_.allocate();
    if (!order) {
        ++orders_rejected_;
        std::cerr << "WARNING: Order pool exhausted, rejecting order_id=" << cmd.order_id << "\n";
        return;
    }
    
    // Initialize order
    order->order_id = cmd.order_id;
    order->side = cmd.side;
    order->price = cmd.price;
    order->quantity = cmd.quantity;
    order->timestamp = cmd.producer_timestamp;
    
    // Store in order map for cancellation lookup
    if (order->order_id < order_map_.size()) {
        order_map_[order->order_id] = order;
    }
    
    // Try to match against opposite side
    match_order(order, processing_start);
    
    // Add remainder to book if any quantity left
    if (order->quantity > 0) {
        book_.add_order(order);
    } else {
        // Order fully matched, return to pool
        if (order->order_id < order_map_.size()) {
            order_map_[order->order_id] = nullptr;
        }
        order_pool_.free(order);
    }
}

void MatchingEngine::handle_cancel_order(uint64_t order_id) noexcept {
    if (order_id >= order_map_.size()) return;
    
    Order* order = order_map_[order_id];
    if (!order) return;  // Order not found or already matched/cancelled
    
    book_.remove_order(order);
    order_map_[order_id] = nullptr;
    order_pool_.free(order);
}

void MatchingEngine::match_order(Order* aggressor, const std::chrono::high_resolution_clock::time_point& processing_start) noexcept {
    if (aggressor->side == Side::BUY) {
        // Buy order: match against asks at or below aggressor's price
        match_against_asks(aggressor, processing_start);
    } else {
        // Sell order: match against bids at or above aggressor's price  
        match_against_bids(aggressor, processing_start);
    }
}

void MatchingEngine::match_against_asks(Order* buy_order, const std::chrono::high_resolution_clock::time_point& processing_start) noexcept {
    // Start from best ask and work up in price
    for (int64_t price = book_.best_ask(); price <= buy_order->price && price != -1; ++price) {
        PriceLevel* level = book_.get_price_level(price, Side::SELL);
        if (!level || level->empty()) continue;
        
        // Match against all orders at this price level in time priority
        Order* ask_order = level->head;
        while (ask_order && buy_order->quantity > 0) {
            Order* next_ask = ask_order->next;  // Save next before potential removal
            
            const uint64_t trade_quantity = std::min(buy_order->quantity, ask_order->quantity);
            execute_trade(buy_order->order_id, ask_order->order_id, price, trade_quantity, processing_start);
            
            buy_order->quantity -= trade_quantity;
            ask_order->quantity -= trade_quantity;
            
            if (ask_order->quantity == 0) {
                // Ask order fully matched, remove from book
                level->remove_order(ask_order);
                if (ask_order->order_id < order_map_.size()) {
                    order_map_[ask_order->order_id] = nullptr;
                }
                order_pool_.free(ask_order);
            }
            
            ask_order = next_ask;
        }
        
        if (buy_order->quantity == 0) break;  // Buy order fully matched
    }
}

void MatchingEngine::match_against_bids(Order* sell_order, const std::chrono::high_resolution_clock::time_point& processing_start) noexcept {
    // Start from best bid and work down in price
    for (int64_t price = book_.best_bid(); price >= sell_order->price && price != -1; --price) {
        PriceLevel* level = book_.get_price_level(price, Side::BUY);
        if (!level || level->empty()) continue;
        
        // Match against all orders at this price level in time priority
        Order* bid_order = level->head;
        while (bid_order && sell_order->quantity > 0) {
            Order* next_bid = bid_order->next;  // Save next before potential removal
            
            const uint64_t trade_quantity = std::min(sell_order->quantity, bid_order->quantity);
            execute_trade(sell_order->order_id, bid_order->order_id, price, trade_quantity, processing_start);
            
            sell_order->quantity -= trade_quantity;
            bid_order->quantity -= trade_quantity;
            
            if (bid_order->quantity == 0) {
                // Bid order fully matched, remove from book
                level->remove_order(bid_order);
                if (bid_order->order_id < order_map_.size()) {
                    order_map_[bid_order->order_id] = nullptr;
                }
                order_pool_.free(bid_order);
            }
            
            bid_order = next_bid;
        }
        
        if (sell_order->quantity == 0) break;  // Sell order fully matched
    }
}

void MatchingEngine::execute_trade(uint64_t aggressor_id, uint64_t resting_id, int64_t price, 
                  uint64_t quantity, const std::chrono::high_resolution_clock::time_point& processing_start) noexcept {
    
    // Calculate latency from processing start to trade execution
    const auto latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now() - processing_start).count();

    trade_latencies_ns_.push_back(latency_ns);
    
    // Update statistics
    ++trades_executed_;
    total_buy_quantity_matched_ += quantity;
    total_sell_quantity_matched_ += quantity;
    
    // Print trade details
    std::cout << "TRADE: aggressor=" << aggressor_id << " resting=" << resting_id 
              << " price=" << price << " qty=" << quantity << "\n";
}

} // namespace OrderBook