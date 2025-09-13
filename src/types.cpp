#include "types.hpp"

namespace OrderBook {

Order::Order() noexcept 
    : order_id(0), side(Side::BUY), order_type(OrderType::LIMIT), price(0), 
      quantity(0), original_quantity(0), status(OrderStatus::PENDING),
      next(nullptr), prev(nullptr) {}

PriceLevel::PriceLevel() noexcept 
    : total_volume(0), head(nullptr), tail(nullptr) {}

void PriceLevel::add_order(Order* order) noexcept {
    if (!head) {
        head = tail = order;
        order->next = order->prev = nullptr;
    } else {
        tail->next = order;
        order->prev = tail;
        order->next = nullptr;
        tail = order;
    }
    total_volume += order->quantity;
}

void PriceLevel::remove_order(Order* order) noexcept {
    if (order->prev) {
        order->prev->next = order->next;
    } else {
        head = order->next;
    }
    
    if (order->next) {
        order->next->prev = order->prev;
    } else {
        tail = order->prev;
    }
    
    total_volume -= order->quantity;
}

bool PriceLevel::empty() const noexcept { 
    return head == nullptr; 
}

} // namespace OrderBook