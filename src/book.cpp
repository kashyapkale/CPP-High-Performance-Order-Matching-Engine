#include "book.hpp"

namespace OrderBook {

Book::Book() 
    : bid_levels_(PRICE_LEVELS), ask_levels_(PRICE_LEVELS), 
      best_bid_price_(-1), best_ask_price_(-1) {}

void Book::add_order(Order* order) noexcept {
    const uint64_t price_index = static_cast<uint64_t>(order->price - PRICE_MIN);
    
    if (order->side == Side::BUY) {
        bid_levels_[price_index].add_order(order);
        if (best_bid_price_ < order->price) {
            best_bid_price_ = order->price;
        }
    } else {
        ask_levels_[price_index].add_order(order);
        if (best_ask_price_ == -1 || best_ask_price_ > order->price) {
            best_ask_price_ = order->price;
        }
    }
}

void Book::remove_order(Order* order) noexcept {
    const uint64_t price_index = static_cast<uint64_t>(order->price - PRICE_MIN);
    
    if (order->side == Side::BUY) {
        bid_levels_[price_index].remove_order(order);
        // Update best bid if this level is now empty and was the best
        if (bid_levels_[price_index].empty() && order->price == best_bid_price_) {
            update_best_bid();
        }
    } else {
        ask_levels_[price_index].remove_order(order);
        // Update best ask if this level is now empty and was the best
        if (ask_levels_[price_index].empty() && order->price == best_ask_price_) {
            update_best_ask();
        }
    }
}

PriceLevel* Book::get_price_level(int64_t price, Side side) noexcept {
    if (price < static_cast<int64_t>(PRICE_MIN) || price > static_cast<int64_t>(PRICE_MAX)) return nullptr;
    
    const uint64_t price_index = static_cast<uint64_t>(price - PRICE_MIN);
    return (side == Side::BUY) ? &bid_levels_[price_index] : &ask_levels_[price_index];
}

int64_t Book::best_bid() const noexcept { 
    return best_bid_price_; 
}

int64_t Book::best_ask() const noexcept { 
    return best_ask_price_; 
}

void Book::update_best_bid() noexcept {
    best_bid_price_ = -1;
    for (int64_t price = static_cast<int64_t>(PRICE_MAX); price >= static_cast<int64_t>(PRICE_MIN); --price) {
        const uint64_t price_index = static_cast<uint64_t>(price - PRICE_MIN);
        if (!bid_levels_[price_index].empty()) {
            best_bid_price_ = price;
            break;
        }
    }
}

void Book::update_best_ask() noexcept {
    best_ask_price_ = -1;
    for (int64_t price = static_cast<int64_t>(PRICE_MIN); price <= static_cast<int64_t>(PRICE_MAX); ++price) {
        const uint64_t price_index = static_cast<uint64_t>(price - PRICE_MIN);
        if (!ask_levels_[price_index].empty()) {
            best_ask_price_ = price;
            break;
        }
    }
}

} // namespace OrderBook