#pragma once

#include <cstdint>
#include <chrono>

namespace OrderBook {

// Configuration constants
constexpr uint64_t PRICE_MIN = 0;
constexpr uint64_t PRICE_MAX = 10000;
constexpr uint64_t PRICE_LEVELS = PRICE_MAX - PRICE_MIN + 1;
constexpr uint64_t MAX_ORDERS = 1000000;
constexpr uint64_t RING_BUFFER_SIZE = 1 << 20;  // 1M entries, power of 2
constexpr uint64_t RING_BUFFER_MASK = RING_BUFFER_SIZE - 1;
constexpr uint64_t TOTAL_ORDERS_TO_GENERATE = 20000000;

// Enumerations
enum class Side : uint8_t {
    BUY,
    SELL
};

enum class CommandType : uint8_t {
    NEW,
    CANCEL
};

// Forward declarations
struct Order;
struct PriceLevel;
class OrderPool;
class Book;
class MatchingEngine;

// Core data structures
struct Order {
    uint64_t order_id;
    Side side;
    int64_t price;
    uint64_t quantity;
    std::chrono::high_resolution_clock::time_point timestamp;
    
    // Intrusive linked list pointers
    Order* next;
    Order* prev;
    
    Order() noexcept;
};

struct Command {
    CommandType type;
    uint64_t order_id;
    Side side;
    int64_t price;
    uint64_t quantity;
    std::chrono::high_resolution_clock::time_point producer_timestamp;
    
    Command() noexcept = default;
};

struct PriceLevel {
    uint64_t total_volume;
    Order* head;  // First order (oldest)
    Order* tail;  // Last order (newest)
    
    PriceLevel() noexcept;
    void add_order(Order* order) noexcept;
    void remove_order(Order* order) noexcept;
    bool empty() const noexcept;
};

} // namespace OrderBook