#include "order_pool.hpp"

namespace OrderBook {

OrderPool::OrderPool(uint64_t max_orders) noexcept 
    : pool_(max_orders), free_head_(nullptr), allocated_count_(0) {
    
    // Initialize free list - all orders are initially free
    // Link them together using the next pointer
    for (uint64_t i = 0; i < max_orders - 1; ++i) {
        pool_[i].next = &pool_[i + 1];
    }
    pool_[max_orders - 1].next = nullptr;
    free_head_ = &pool_[0];
}

Order* OrderPool::allocate() noexcept {
    if (!free_head_) return nullptr;
    
    Order* order = free_head_;
    free_head_ = free_head_->next;
    
    // Clear the order for reuse
    order->next = nullptr;
    order->prev = nullptr;
    ++allocated_count_;
    
    return order;
}

void OrderPool::free(Order* order) noexcept {
    if (!order) return;
    
    order->next = free_head_;
    free_head_ = order;
    --allocated_count_;
}

uint64_t OrderPool::allocated_count() const noexcept { 
    return allocated_count_; 
}

uint64_t OrderPool::available_count() const noexcept { 
    return pool_.size() - allocated_count_; 
}

} // namespace OrderBook