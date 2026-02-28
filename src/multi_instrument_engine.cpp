#include "multi_instrument_engine.hpp"
#include <iostream>
#include <algorithm>

namespace OrderBook {

MultiInstrumentEngine::MultiInstrumentEngine(SPSCRingBuffer* ring_buffer) 
    : order_pool_(std::make_unique<OrderPool>(MAX_ORDERS)), 
      ring_buffer_(ring_buffer),
      order_map_(MAX_ORDERS, {nullptr, 0}),
      orders_processed_(0),
      total_trades_executed_(0),
      orders_rejected_(0) {
    
    trade_latencies_ns_.reserve(TOTAL_ORDERS_TO_GENERATE / 10);
}

bool MultiInstrumentEngine::add_instrument(const Instrument& instrument) {
    if (instruments_.find(instrument.instrument_id) != instruments_.end()) {
        return false; // Instrument already exists
    }
    
    instruments_[instrument.instrument_id] = instrument;
    books_[instrument.instrument_id] = std::make_unique<Book>();
    trades_per_instrument_[instrument.instrument_id] = 0;
    volume_per_instrument_[instrument.instrument_id] = 0;
    
    return true;
}

bool MultiInstrumentEngine::remove_instrument(uint32_t instrument_id) {
    auto it = instruments_.find(instrument_id);
    if (it == instruments_.end()) {
        return false;
    }
    
    // TODO: Ensure all orders for this instrument are processed/cancelled
    instruments_.erase(it);
    books_.erase(instrument_id);
    trades_per_instrument_.erase(instrument_id);
    volume_per_instrument_.erase(instrument_id);
    
    return true;
}

void MultiInstrumentEngine::run() noexcept {
    Command cmd;  // Using original command type for compatibility
    
    while (orders_processed_ < TOTAL_ORDERS_TO_GENERATE) {
        if (ring_buffer_->dequeue(cmd)) {
            const auto processing_start = std::chrono::high_resolution_clock::now();
            
            // Convert to multi-instrument command (assume instrument_id = 1 for compatibility)
            MultiInstrumentCommand multi_cmd;
            multi_cmd.type = cmd.type;
            multi_cmd.instrument_id = 1; // Default instrument for backward compatibility
            multi_cmd.order_id = cmd.order_id;
            multi_cmd.side = cmd.side;
            multi_cmd.price = cmd.price;
            multi_cmd.quantity = cmd.quantity;
            multi_cmd.producer_timestamp = cmd.producer_timestamp;
            
            if (multi_cmd.type == CommandType::NEW) {
                handle_new_order(multi_cmd, processing_start);
            } else {
                handle_cancel_order(multi_cmd.instrument_id, multi_cmd.order_id);
            }
            
            ++orders_processed_;
        }
    }
}

const Book* MultiInstrumentEngine::get_book(uint32_t instrument_id) const noexcept {
    auto it = books_.find(instrument_id);
    return (it != books_.end()) ? it->second.get() : nullptr;
}

uint64_t MultiInstrumentEngine::orders_processed() const noexcept {
    return orders_processed_;
}

uint64_t MultiInstrumentEngine::total_trades_executed() const noexcept {
    return total_trades_executed_;
}

uint64_t MultiInstrumentEngine::orders_rejected() const noexcept {
    return orders_rejected_;
}

uint64_t MultiInstrumentEngine::trades_for_instrument(uint32_t instrument_id) const noexcept {
    auto it = trades_per_instrument_.find(instrument_id);
    return (it != trades_per_instrument_.end()) ? it->second : 0;
}

uint64_t MultiInstrumentEngine::volume_for_instrument(uint32_t instrument_id) const noexcept {
    auto it = volume_per_instrument_.find(instrument_id);
    return (it != volume_per_instrument_.end()) ? it->second : 0;
}

const std::vector<long long>& MultiInstrumentEngine::trade_latencies() const noexcept {
    return trade_latencies_ns_;
}

void MultiInstrumentEngine::handle_new_order(const MultiInstrumentCommand& cmd, 
                                            const std::chrono::high_resolution_clock::time_point& processing_start) noexcept {
    // Validate order
    if (!validate_order(cmd)) {
        return;
    }
    
    auto book_it = books_.find(cmd.instrument_id);
    if (book_it == books_.end()) {
        return; // Unknown instrument
    }
    
    Order* order = order_pool_->allocate();
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
    
    // Store in order map with instrument mapping
    if (order->order_id < order_map_.size()) {
        order_map_[order->order_id] = {order, cmd.instrument_id};
    }
    
    // Try to match against opposite side
    match_order(cmd.instrument_id, order, processing_start);
    
    // Add remainder to book if any quantity left
    if (order->quantity > 0) {
        book_it->second->add_order(order);
    } else {
        // Order fully matched, return to pool
        if (order->order_id < order_map_.size()) {
            order_map_[order->order_id] = {nullptr, 0};
        }
        order_pool_->free(order);
    }
}

void MultiInstrumentEngine::handle_cancel_order(uint32_t instrument_id, uint64_t order_id) noexcept {
    if (order_id >= order_map_.size()) return;
    
    auto& [order, stored_instrument_id] = order_map_[order_id];
    if (!order || stored_instrument_id != instrument_id) return;
    
    auto book_it = books_.find(instrument_id);
    if (book_it == books_.end()) return;
    
    book_it->second->remove_order(order);
    order_map_[order_id] = {nullptr, 0};
    order_pool_->free(order);
}

bool MultiInstrumentEngine::validate_order(const MultiInstrumentCommand& cmd) const noexcept {
    auto it = instruments_.find(cmd.instrument_id);
    if (it == instruments_.end()) return false;
    
    const Instrument& instrument = it->second;
    return instrument.is_valid_price(cmd.price) && instrument.is_valid_quantity(cmd.quantity);
}

void MultiInstrumentEngine::execute_trade(uint32_t instrument_id, uint64_t aggressor_id, uint64_t resting_id, 
                                        int64_t price, uint64_t quantity,
                                        const std::chrono::high_resolution_clock::time_point& processing_start) noexcept {
    // Calculate latency from processing start to trade execution
    const auto latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now() - processing_start).count();

    trade_latencies_ns_.push_back(latency_ns);
    
    // Update statistics
    ++total_trades_executed_;
    trades_per_instrument_[instrument_id]++;
    volume_per_instrument_[instrument_id] += quantity;
    
    // Get instrument symbol for output
    const std::string& symbol = instruments_.at(instrument_id).symbol;
    
    // Print trade details with instrument information
    std::cout << "TRADE: " << symbol << " aggressor=" << aggressor_id 
              << " resting=" << resting_id << " price=" << price 
              << " qty=" << quantity << "\n";
}

void MultiInstrumentEngine::match_order(uint32_t instrument_id, Order* order, 
                                       const std::chrono::high_resolution_clock::time_point& processing_start) noexcept {
    auto book_it = books_.find(instrument_id);
    if (book_it == books_.end()) return;
    
    Book* book = book_it->second.get();
    
    if (order->side == Side::BUY) {
        // Match against asks
        for (int64_t price = book->best_ask(); price <= order->price && price != -1; ++price) {
            PriceLevel* level = book->get_price_level(price, Side::SELL);
            if (!level || level->empty()) continue;
            
            Order* ask_order = level->head;
            while (ask_order && order->quantity > 0) {
                Order* next_ask = ask_order->next;
                
                const uint64_t trade_quantity = std::min(order->quantity, ask_order->quantity);
                execute_trade(instrument_id, order->order_id, ask_order->order_id, 
                            price, trade_quantity, processing_start);
                
                order->quantity -= trade_quantity;
                ask_order->quantity -= trade_quantity;
                
                if (ask_order->quantity == 0) {
                    level->remove_order(ask_order);
                    if (ask_order->order_id < order_map_.size()) {
                        order_map_[ask_order->order_id] = {nullptr, 0};
                    }
                    order_pool_->free(ask_order);
                }
                
                ask_order = next_ask;
            }
            
            if (order->quantity == 0) break;
        }
    } else {
        // Match against bids
        for (int64_t price = book->best_bid(); price >= order->price && price != -1; --price) {
            PriceLevel* level = book->get_price_level(price, Side::BUY);
            if (!level || level->empty()) continue;
            
            Order* bid_order = level->head;
            while (bid_order && order->quantity > 0) {
                Order* next_bid = bid_order->next;
                
                const uint64_t trade_quantity = std::min(order->quantity, bid_order->quantity);
                execute_trade(instrument_id, order->order_id, bid_order->order_id, 
                            price, trade_quantity, processing_start);
                
                order->quantity -= trade_quantity;
                bid_order->quantity -= trade_quantity;
                
                if (bid_order->quantity == 0) {
                    level->remove_order(bid_order);
                    if (bid_order->order_id < order_map_.size()) {
                        order_map_[bid_order->order_id] = {nullptr, 0};
                    }
                    order_pool_->free(bid_order);
                }
                
                bid_order = next_bid;
            }
            
            if (order->quantity == 0) break;
        }
    }
}

// Multi-instrument ring buffer implementation
MultiInstrumentRingBuffer::MultiInstrumentRingBuffer() 
    : head_(0), tail_(0), buffer_(RING_BUFFER_SIZE) {}

bool MultiInstrumentRingBuffer::enqueue(const MultiInstrumentCommand& cmd) noexcept {
    const uint64_t current_head = head_.load(std::memory_order_relaxed);
    const uint64_t next_head = (current_head + 1) & RING_BUFFER_MASK;
    
    if (next_head == tail_.load(std::memory_order_acquire)) {
        return false;
    }
    
    buffer_[current_head] = cmd;
    head_.store(next_head, std::memory_order_release);
    return true;
}

bool MultiInstrumentRingBuffer::dequeue(MultiInstrumentCommand& cmd) noexcept {
    const uint64_t current_tail = tail_.load(std::memory_order_relaxed);
    
    if (current_tail == head_.load(std::memory_order_acquire)) {
        return false;
    }
    
    cmd = buffer_[current_tail];
    tail_.store((current_tail + 1) & RING_BUFFER_MASK, std::memory_order_release);
    return true;
}

} // namespace OrderBook