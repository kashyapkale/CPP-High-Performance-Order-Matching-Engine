#pragma once

#include "types.hpp"
#include <vector>

namespace OrderBook {

/**
 * Object pool for Orders to eliminate dynamic allocation on critical path.
 * Uses an intrusive free list for O(1) allocation/deallocation.
 * Pre-allocates all Order objects at startup.
 */
class OrderPool {
private:
    std::vector<Order> pool_;
    Order* free_head_;
    uint64_t allocated_count_;
    
public:
    explicit OrderPool(uint64_t max_orders) noexcept;
    
    /**
     * Allocate an Order from the pool. Returns nullptr if pool is exhausted.
     * O(1) operation - just pop from free list head.
     */
    Order* allocate() noexcept;
    
    /**
     * Return an Order to the pool for reuse.
     * O(1) operation - push to free list head.
     */
    void free(Order* order) noexcept;
    
    uint64_t allocated_count() const noexcept;
    uint64_t available_count() const noexcept;
};

} // namespace OrderBook