#include "enhanced_matching_engine.hpp"
#include <iostream>
#include <algorithm>

namespace OrderBook {

EnhancedMatchingEngine::EnhancedMatchingEngine(SPSCRingBuffer* ring_buffer) 
    : order_pool_(MAX_ORDERS), ring_buffer_(ring_buffer),
      order_map_(MAX_ORDERS, nullptr), orders_processed_(0), 
      trades_executed_(0), total_buy_quantity_matched_(0), 
      total_sell_quantity_matched_(0) {
    
    trade_latencies_ns_.reserve(TOTAL_ORDERS_TO_GENERATE / 10);
    
    // Initialize order type statistics
    for (auto& stats : order_type_stats_) {
        stats = OrderTypeStats{};
    }
}

void EnhancedMatchingEngine::set_market_data_manager(std::unique_ptr<MarketDataManager> manager) {
    market_data_manager_ = std::move(manager);
}

void EnhancedMatchingEngine::run() noexcept {
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
    }
}

uint64_t EnhancedMatchingEngine::orders_processed() const noexcept {
    return orders_processed_;
}

uint64_t EnhancedMatchingEngine::trades_executed() const noexcept {
    return trades_executed_;
}

const std::vector<long long>& EnhancedMatchingEngine::trade_latencies() const noexcept {
    return trade_latencies_ns_;
}

uint64_t EnhancedMatchingEngine::total_buy_quantity_matched() const noexcept {
    return total_buy_quantity_matched_;
}

uint64_t EnhancedMatchingEngine::total_sell_quantity_matched() const noexcept {
    return total_sell_quantity_matched_;
}

const EnhancedMatchingEngine::OrderTypeStats& EnhancedMatchingEngine::get_order_type_stats(OrderType type) const noexcept {
    return order_type_stats_[static_cast<size_t>(type)];
}

void EnhancedMatchingEngine::print_order_type_statistics() const noexcept {
    const char* order_type_names[] = {"LIMIT", "IOC", "FOK"};
    
    std::cout << "\n=== ORDER TYPE STATISTICS ===\n";
    for (int i = 0; i < 3; ++i) {
        const auto& stats = order_type_stats_[i];
        std::cout << order_type_names[i] << " Orders:\n";
        std::cout << "  Submitted: " << stats.submitted << "\n";
        std::cout << "  Filled: " << stats.filled << "\n";
        std::cout << "  Partial Fills: " << stats.partial_fills << "\n";
        std::cout << "  Cancelled: " << stats.cancelled << "\n";
        std::cout << "  Rejected: " << stats.rejected << "\n";
        
        if (stats.submitted > 0) {
            double fill_rate = (double)(stats.filled + stats.partial_fills) / stats.submitted * 100.0;
            std::cout << "  Fill Rate: " << std::fixed << std::setprecision(2) << fill_rate << "%\n";
        }
        std::cout << "\n";
    }
}

Level2Snapshot EnhancedMatchingEngine::create_level2_snapshot() const noexcept {
    Level2Snapshot snapshot(1, "DEFAULT");
    
    // Collect bid levels (highest to lowest)
    for (int64_t price = PRICE_MAX; price >= PRICE_MIN; --price) {
        PriceLevel* level = book_.get_price_level(price, Side::BUY);
        if (level && !level->empty()) {
            uint32_t order_count = 0;
            Order* order = level->head;
            while (order) {
                ++order_count;
                order = order->next;
            }
            snapshot.bids.emplace_back(price, level->total_volume, order_count);
            
            if (snapshot.bids.size() >= 20) break; // Top 20 levels
        }
    }
    
    // Collect ask levels (lowest to highest)
    for (int64_t price = PRICE_MIN; price <= PRICE_MAX; ++price) {
        PriceLevel* level = book_.get_price_level(price, Side::SELL);
        if (level && !level->empty()) {
            uint32_t order_count = 0;
            Order* order = level->head;
            while (order) {
                ++order_count;
                order = order->next;
            }
            snapshot.asks.emplace_back(price, level->total_volume, order_count);
            
            if (snapshot.asks.size() >= 20) break; // Top 20 levels
        }
    }
    
    return snapshot;
}

void EnhancedMatchingEngine::handle_new_order(const Command& cmd, const std::chrono::high_resolution_clock::time_point& processing_start) noexcept {
    Order* order = order_pool_.allocate();
    if (!order) {
        return; // Pool exhausted
    }
    
    // Initialize order
    order->order_id = cmd.order_id;
    order->side = cmd.side;
    order->order_type = cmd.order_type;
    order->price = cmd.price;
    order->quantity = cmd.quantity;
    order->original_quantity = cmd.quantity;
    order->status = OrderStatus::PENDING;
    order->timestamp = cmd.producer_timestamp;
    
    // Update statistics
    order_type_stats_[static_cast<size_t>(cmd.order_type)].submitted++;
    
    // Store in order map for cancellation lookup
    if (order->order_id < order_map_.size()) {
        order_map_[order->order_id] = order;
    }
    
    // Try to match order based on type
    MatchResult result = match_order(order, processing_start);
    
    // Handle result based on order type
    switch (order->order_type) {
        case OrderType::LIMIT:
            if (result == MatchResult::PARTIALLY_MATCHED || result == MatchResult::NO_MATCH) {
                // Add remainder to book
                if (order->quantity > 0) {
                    book_.add_order(order);
                    order->status = (result == MatchResult::PARTIALLY_MATCHED) ? 
                        OrderStatus::PARTIAL_FILL : OrderStatus::PENDING;
                }
            }
            break;
            
        case OrderType::IOC:
            // IOC orders: match what we can, cancel the rest
            if (order->quantity > 0) {
                order->status = OrderStatus::CANCELLED;
                order_type_stats_[static_cast<size_t>(OrderType::IOC)].cancelled++;
                order_pool_.free(order);
                if (order->order_id < order_map_.size()) {
                    order_map_[order->order_id] = nullptr;
                }
            }
            break;
            
        case OrderType::FOK:
            // FOK orders: already handled in match_fok_order
            if (result == MatchResult::REJECTED) {
                order_pool_.free(order);
                if (order->order_id < order_map_.size()) {
                    order_map_[order->order_id] = nullptr;
                }
            }
            break;
    }
    
    // Update final status if order was fully matched
    if (order->quantity == 0 && order->status != OrderStatus::CANCELLED && 
        order->status != OrderStatus::REJECTED) {
        order->status = OrderStatus::FILLED;
        order_type_stats_[static_cast<size_t>(order->order_type)].filled++;
        
        // Remove from book and return to pool
        if (order->order_id < order_map_.size()) {
            order_map_[order->order_id] = nullptr;
        }
        order_pool_.free(order);
    } else if (result == MatchResult::PARTIALLY_MATCHED) {
        order_type_stats_[static_cast<size_t>(order->order_type)].partial_fills++;
    }
}

void EnhancedMatchingEngine::handle_cancel_order(uint64_t order_id) noexcept {
    if (order_id >= order_map_.size()) return;
    
    Order* order = order_map_[order_id];
    if (!order) return;
    
    book_.remove_order(order);
    order->status = OrderStatus::CANCELLED;
    order_type_stats_[static_cast<size_t>(order->order_type)].cancelled++;
    
    order_map_[order_id] = nullptr;
    order_pool_.free(order);
}

EnhancedMatchingEngine::MatchResult EnhancedMatchingEngine::match_order(Order* order, const std::chrono::high_resolution_clock::time_point& processing_start) noexcept {
    switch (order->order_type) {
        case OrderType::LIMIT:
            return match_limit_order(order, processing_start);
        case OrderType::IOC:
            return match_ioc_order(order, processing_start);
        case OrderType::FOK:
            return match_fok_order(order, processing_start);
        default:
            return MatchResult::REJECTED;
    }
}

EnhancedMatchingEngine::MatchResult EnhancedMatchingEngine::match_limit_order(Order* order, const std::chrono::high_resolution_clock::time_point& processing_start) noexcept {
    uint64_t original_quantity = order->quantity;
    
    if (order->side == Side::BUY) {
        match_against_asks(order, processing_start);
    } else {
        match_against_bids(order, processing_start);
    }
    
    if (order->quantity == 0) {
        return MatchResult::FULLY_MATCHED;
    } else if (order->quantity < original_quantity) {
        return MatchResult::PARTIALLY_MATCHED;
    } else {
        return MatchResult::NO_MATCH;
    }
}

EnhancedMatchingEngine::MatchResult EnhancedMatchingEngine::match_ioc_order(Order* order, const std::chrono::high_resolution_clock::time_point& processing_start) noexcept {
    // IOC: Match immediately, cancel remainder
    return match_limit_order(order, processing_start);
}

EnhancedMatchingEngine::MatchResult EnhancedMatchingEngine::match_fok_order(Order* order, const std::chrono::high_resolution_clock::time_point& processing_start) noexcept {
    // FOK: Fill completely or reject
    if (!can_fill_completely(order)) {
        reject_order(order, "FOK order cannot be completely filled");
        return MatchResult::REJECTED;
    }
    
    // If we can fill completely, match like a normal order
    MatchResult result = match_limit_order(order, processing_start);
    
    if (result != MatchResult::FULLY_MATCHED) {
        // This shouldn't happen if can_fill_completely worked correctly
        reject_order(order, "FOK order partially filled - algorithmic error");
        return MatchResult::REJECTED;
    }
    
    return MatchResult::FULLY_MATCHED;
}

bool EnhancedMatchingEngine::can_fill_completely(Order* order) const noexcept {
    uint64_t fillable_quantity = calculate_fillable_quantity(order);
    return fillable_quantity >= order->quantity;
}

uint64_t EnhancedMatchingEngine::calculate_fillable_quantity(Order* order) const noexcept {
    uint64_t fillable = 0;
    
    if (order->side == Side::BUY) {
        // Check ask side
        for (int64_t price = book_.best_ask(); price <= order->price && price != -1; ++price) {
            PriceLevel* level = book_.get_price_level(price, Side::SELL);
            if (level && !level->empty()) {
                fillable += level->total_volume;
                if (fillable >= order->quantity) {
                    return fillable;
                }
            }
        }
    } else {
        // Check bid side
        for (int64_t price = book_.best_bid(); price >= order->price && price != -1; --price) {
            PriceLevel* level = book_.get_price_level(price, Side::BUY);
            if (level && !level->empty()) {
                fillable += level->total_volume;
                if (fillable >= order->quantity) {
                    return fillable;
                }
            }
        }
    }
    
    return fillable;
}

void EnhancedMatchingEngine::match_against_asks(Order* buy_order, const std::chrono::high_resolution_clock::time_point& processing_start) noexcept {
    for (int64_t price = book_.best_ask(); price <= buy_order->price && price != -1; ++price) {
        PriceLevel* level = book_.get_price_level(price, Side::SELL);
        if (!level || level->empty()) continue;
        
        Order* ask_order = level->head;
        while (ask_order && buy_order->quantity > 0) {
            Order* next_ask = ask_order->next;
            
            const uint64_t trade_quantity = std::min(buy_order->quantity, ask_order->quantity);
            execute_trade(buy_order->order_id, ask_order->order_id, price, trade_quantity, processing_start);
            
            buy_order->quantity -= trade_quantity;
            ask_order->quantity -= trade_quantity;
            
            if (ask_order->quantity == 0) {
                level->remove_order(ask_order);
                ask_order->status = OrderStatus::FILLED;
                if (ask_order->order_id < order_map_.size()) {
                    order_map_[ask_order->order_id] = nullptr;
                }
                order_pool_.free(ask_order);
            } else {
                ask_order->status = OrderStatus::PARTIAL_FILL;
            }
            
            ask_order = next_ask;
        }
        
        // Publish market data update for this price level
        publish_market_data_update(Side::SELL, price);
        
        if (buy_order->quantity == 0) break;
    }
}

void EnhancedMatchingEngine::match_against_bids(Order* sell_order, const std::chrono::high_resolution_clock::time_point& processing_start) noexcept {
    for (int64_t price = book_.best_bid(); price >= sell_order->price && price != -1; --price) {
        PriceLevel* level = book_.get_price_level(price, Side::BUY);
        if (!level || level->empty()) continue;
        
        Order* bid_order = level->head;
        while (bid_order && sell_order->quantity > 0) {
            Order* next_bid = bid_order->next;
            
            const uint64_t trade_quantity = std::min(sell_order->quantity, bid_order->quantity);
            execute_trade(sell_order->order_id, bid_order->order_id, price, trade_quantity, processing_start);
            
            sell_order->quantity -= trade_quantity;
            bid_order->quantity -= trade_quantity;
            
            if (bid_order->quantity == 0) {
                level->remove_order(bid_order);
                bid_order->status = OrderStatus::FILLED;
                if (bid_order->order_id < order_map_.size()) {
                    order_map_[bid_order->order_id] = nullptr;
                }
                order_pool_.free(bid_order);
            } else {
                bid_order->status = OrderStatus::PARTIAL_FILL;
            }
            
            bid_order = next_bid;
        }
        
        // Publish market data update for this price level
        publish_market_data_update(Side::BUY, price);
        
        if (sell_order->quantity == 0) break;
    }
}

void EnhancedMatchingEngine::execute_trade(uint64_t aggressor_id, uint64_t resting_id, int64_t price, 
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
    
    // Publish to market data if available
    if (market_data_manager_) {
        Trade trade(1, "DEFAULT", aggressor_id, resting_id, Side::BUY, price, quantity);
        market_data_manager_->publish_trade(trade);
    }
}

void EnhancedMatchingEngine::publish_market_data_update(Side side, int64_t price) noexcept {
    if (!market_data_manager_) return;
    
    PriceLevel* level = book_.get_price_level(price, side);
    uint32_t order_count = 0;
    
    if (level) {
        Order* order = level->head;
        while (order) {
            ++order_count;
            order = order->next;
        }
        
        market_data_manager_->publish_level2_update(1, "DEFAULT", side, price, 
                                                   level->total_volume, order_count);
    } else {
        // Level is empty
        market_data_manager_->publish_level2_update(1, "DEFAULT", side, price, 0, 0);
    }
}

void EnhancedMatchingEngine::reject_order(Order* order, const std::string& reason) noexcept {
    order->status = OrderStatus::REJECTED;
    order_type_stats_[static_cast<size_t>(order->order_type)].rejected++;
    
    std::cout << "ORDER REJECTED: id=" << order->order_id << " reason=" << reason << std::endl;
}

} // namespace OrderBook