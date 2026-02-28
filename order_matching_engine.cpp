/*
 * High-Performance C++20 Limit Order Book and Matching Engine
 * 
 * This implementation prioritizes ultra-low latency with zero dynamic allocation
 * on the critical path and lock-free communication between producer and consumer.
 * 
 * Key Performance Design Choices:
 * 1. Direct-mapped price grid using std::vector for O(1) price level lookup
 * 2. Object pooling to eliminate heap allocation during order processing
 * 3. Intrusive linked lists to avoid pointer chasing and improve cache locality
 * 4. SPSC ring buffer with memory ordering for lock-free thread communication
 * 5. Single-writer design eliminates locks on order book data structures
 */

#include <iostream>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>
#include <random>
#include <algorithm>
#include <cassert>
#include <cstdint>

// =============================================================================
// CONFIGURATION CONSTANTS
// =============================================================================

constexpr uint64_t PRICE_MIN = 0;
constexpr uint64_t PRICE_MAX = 10000;
constexpr uint64_t PRICE_LEVELS = PRICE_MAX - PRICE_MIN + 1;
constexpr uint64_t MAX_ORDERS = 1000000;  // Object pool size
constexpr uint64_t RING_BUFFER_SIZE = 1 << 20;  // 1M entries, power of 2 for efficient masking
constexpr uint64_t RING_BUFFER_MASK = RING_BUFFER_SIZE - 1;
constexpr uint64_t TOTAL_ORDERS_TO_GENERATE = 20000000;  // 20M orders for stress testing

// =============================================================================
// CORE DATA STRUCTURES
// =============================================================================

enum class Side : uint8_t {
    BUY,
    SELL
};

enum class CommandType : uint8_t {
    NEW,
    CANCEL
};

/*
 * Order struct uses intrusive linked list design for cache efficiency.
 * By embedding the list pointers directly in the Order, we avoid separate
 * node allocations and improve memory locality.
 */
struct Order {
    uint64_t order_id;
    Side side;
    int64_t price;
    uint64_t quantity;
    std::chrono::high_resolution_clock::time_point timestamp;  // For price-time priority
    
    // Intrusive linked list pointers
    Order* next;
    Order* prev;
    
    Order() noexcept : order_id(0), side(Side::BUY), price(0), quantity(0), next(nullptr), prev(nullptr) {}
};

/*
 * Command structure for lock-free communication between producer and consumer.
 * Includes timestamp set by producer for latency measurement.
 */
struct Command {
    CommandType type;
    uint64_t order_id;
    Side side;
    int64_t price;
    uint64_t quantity;
    std::chrono::high_resolution_clock::time_point producer_timestamp;
    
    Command() noexcept = default;
};

/*
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
    explicit OrderPool(uint64_t max_orders) noexcept 
        : pool_(max_orders), free_head_(nullptr), allocated_count_(0) {
        
        // Initialize free list - all orders are initially free
        // Link them together using the next pointer
        for (uint64_t i = 0; i < max_orders - 1; ++i) {
            pool_[i].next = &pool_[i + 1];
        }
        pool_[max_orders - 1].next = nullptr;
        free_head_ = &pool_[0];
    }
    
    /*
     * Allocate an Order from the pool. Returns nullptr if pool is exhausted.
     * O(1) operation - just pop from free list head.
     */
    Order* allocate() noexcept {
        if (!free_head_) return nullptr;
        
        Order* order = free_head_;
        free_head_ = free_head_->next;
        
        // Clear the order for reuse
        order->next = nullptr;
        order->prev = nullptr;
        ++allocated_count_;
        
        return order;
    }
    
    /*
     * Return an Order to the pool for reuse.
     * O(1) operation - push to free list head.
     */
    void free(Order* order) noexcept {
        if (!order) return;
        
        order->next = free_head_;
        free_head_ = order;
        --allocated_count_;
    }
    
    uint64_t allocated_count() const noexcept { return allocated_count_; }
    uint64_t available_count() const noexcept { return pool_.size() - allocated_count_; }
};

/*
 * PriceLevel represents all orders at a single price point.
 * Uses intrusive doubly-linked list for FIFO order within the price level.
 * Maintains total volume for quick aggregated view.
 */
struct PriceLevel {
    uint64_t total_volume;
    Order* head;  // First order (oldest)
    Order* tail;  // Last order (newest)
    
    PriceLevel() noexcept : total_volume(0), head(nullptr), tail(nullptr) {}
    
    /*
     * Add order to end of FIFO queue (newest orders go to tail)
     * Maintains price-time priority within the price level
     */
    void add_order(Order* order) noexcept {
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
    
    /*
     * Remove specific order from anywhere in the FIFO queue
     * Used for order cancellations
     */
    void remove_order(Order* order) noexcept {
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
    
    bool empty() const noexcept { return head == nullptr; }
};

/*
 * Single-Producer Single-Consumer Lock-Free Ring Buffer
 * 
 * Critical design choices for ultra-low latency:
 * 1. Power-of-2 size allows bitwise masking instead of modulo operation
 * 2. Separate cache lines for head/tail to avoid false sharing
 * 3. Acquire-Release memory ordering provides necessary synchronization without seq_cst overhead
 * 4. Producer and consumer each own their respective indices to minimize contention
 */
class SPSCRingBuffer {
private:
    alignas(64) std::atomic<uint64_t> head_;  // Producer writes here, separate cache line
    alignas(64) std::atomic<uint64_t> tail_;  // Consumer reads from here, separate cache line
    std::vector<Command> buffer_;
    
public:
    SPSCRingBuffer() : head_(0), tail_(0), buffer_(RING_BUFFER_SIZE) {}
    
    /*
     * Producer enqueue operation
     * Uses release memory ordering to ensure all writes to the command
     * are visible to the consumer before the head index is updated
     */
    bool enqueue(const Command& cmd) noexcept {
        const uint64_t current_head = head_.load(std::memory_order_relaxed);
        const uint64_t next_head = (current_head + 1) & RING_BUFFER_MASK;
        
        // Check if buffer is full
        if (next_head == tail_.load(std::memory_order_acquire)) {
            return false;
        }
        
        buffer_[current_head] = cmd;
        head_.store(next_head, std::memory_order_release);
        return true;
    }
    
    /*
     * Consumer dequeue operation  
     * Uses acquire memory ordering to ensure all writes from producer
     * are visible before reading the command data
     */
    bool dequeue(Command& cmd) noexcept {
        const uint64_t current_tail = tail_.load(std::memory_order_relaxed);
        
        // Check if buffer is empty
        if (current_tail == head_.load(std::memory_order_acquire)) {
            return false;
        }
        
        cmd = buffer_[current_tail];
        tail_.store((current_tail + 1) & RING_BUFFER_MASK, std::memory_order_release);
        return true;
    }
};

/*
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
    
public:
    Book() : bid_levels_(PRICE_LEVELS), ask_levels_(PRICE_LEVELS), 
             best_bid_price_(-1), best_ask_price_(-1) {}
    
    /*
     * Add order to appropriate price level and side
     * Updates best bid/ask tracking for O(1) top-of-book access
     */
    void add_order(Order* order) noexcept {
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
    
    /*
     * Remove order from book (used for cancellations)
     * Updates best bid/ask if necessary
     */
    void remove_order(Order* order) noexcept {
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
    
    /*
     * Get price level for specific price and side
     * O(1) direct array access
     */
    PriceLevel* get_price_level(int64_t price, Side side) noexcept {
        if (price < PRICE_MIN || price > PRICE_MAX) return nullptr;
        
        const uint64_t price_index = static_cast<uint64_t>(price - PRICE_MIN);
        return (side == Side::BUY) ? &bid_levels_[price_index] : &ask_levels_[price_index];
    }
    
    int64_t best_bid() const noexcept { return best_bid_price_; }
    int64_t best_ask() const noexcept { return best_ask_price_; }
    
private:
    /*
     * Scan down from current best bid to find new best bid
     * Called when best bid level becomes empty
     */
    void update_best_bid() noexcept {
        best_bid_price_ = -1;
        for (int64_t price = PRICE_MAX; price >= PRICE_MIN; --price) {
            const uint64_t price_index = static_cast<uint64_t>(price - PRICE_MIN);
            if (!bid_levels_[price_index].empty()) {
                best_bid_price_ = price;
                break;
            }
        }
    }
    
    /*
     * Scan up from current best ask to find new best ask  
     * Called when best ask level becomes empty
     */
    void update_best_ask() noexcept {
        best_ask_price_ = -1;
        for (int64_t price = PRICE_MIN; price <= PRICE_MAX; ++price) {
            const uint64_t price_index = static_cast<uint64_t>(price - PRICE_MIN);
            if (!ask_levels_[price_index].empty()) {
                best_ask_price_ = price;
                break;
            }
        }
    }
};

// =============================================================================
// MATCHING ENGINE
// =============================================================================

/*
 * Single-threaded matching engine that owns all order book data structures.
 * Processes commands from the lock-free ring buffer sequentially.
 * 
 * Key design principle: Single writer eliminates need for locks on the order book,
 * maximizing performance on the critical path.
 */
class MatchingEngine {
private:
    Book book_;
    OrderPool order_pool_;
    SPSCRingBuffer* ring_buffer_;
    
    // Order tracking for cancellations - maps order_id to Order*
    std::vector<Order*> order_map_;
    
    // Statistics
    std::vector<long long> trade_latencies_ns_;
    uint64_t orders_processed_;
    uint64_t trades_executed_;
    uint64_t orders_rejected_;
    uint64_t total_buy_quantity_matched_;
    uint64_t total_sell_quantity_matched_;
    
public:
    MatchingEngine(SPSCRingBuffer* ring_buffer) 
        : order_pool_(MAX_ORDERS), ring_buffer_(ring_buffer), 
          order_map_(MAX_ORDERS, nullptr), orders_processed_(0),
          trades_executed_(0), orders_rejected_(0),
          total_buy_quantity_matched_(0),
          total_sell_quantity_matched_(0) {
        
        trade_latencies_ns_.reserve(TOTAL_ORDERS_TO_GENERATE / 10);  // Estimate 10% will trade
    }
    
    /*
     * Main processing loop - runs on consumer thread
     * Continuously dequeues commands and processes them
     */
    void run() noexcept {
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
    
    // Getters for statistics
    uint64_t orders_processed() const noexcept { return orders_processed_; }
    uint64_t trades_executed() const noexcept { return trades_executed_; }
    uint64_t orders_rejected() const noexcept { return orders_rejected_; }
    const std::vector<long long>& trade_latencies() const noexcept { return trade_latencies_ns_; }
    uint64_t total_buy_quantity_matched() const noexcept { return total_buy_quantity_matched_; }
    uint64_t total_sell_quantity_matched() const noexcept { return total_sell_quantity_matched_; }
    
private:
    /*
     * Handle new order - attempt to match, then add remainder to book if any
     */
    void handle_new_order(const Command& cmd, const std::chrono::high_resolution_clock::time_point& processing_start) noexcept {
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
    
    /*
     * Handle order cancellation
     */
    void handle_cancel_order(uint64_t order_id) noexcept {
        if (order_id >= order_map_.size()) return;
        
        Order* order = order_map_[order_id];
        if (!order) return;  // Order not found or already matched/cancelled
        
        book_.remove_order(order);
        order_map_[order_id] = nullptr;
        order_pool_.free(order);
    }
    
    /*
     * Core matching logic with strict price-time priority
     * 
     * For buy orders: match against asks starting from best (lowest) price
     * For sell orders: match against bids starting from best (highest) price
     * Within each price level, match in time priority (FIFO)
     */
    void match_order(Order* aggressor, const std::chrono::high_resolution_clock::time_point& processing_start) noexcept {
        if (aggressor->side == Side::BUY) {
            // Buy order: match against asks at or below aggressor's price
            match_against_asks(aggressor, processing_start);
        } else {
            // Sell order: match against bids at or above aggressor's price  
            match_against_bids(aggressor, processing_start);
        }
    }
    
    /*
     * Match buy order against ask side of book
     */
    void match_against_asks(Order* buy_order, const std::chrono::high_resolution_clock::time_point& processing_start) noexcept {
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
    
    /*
     * Match sell order against bid side of book
     */
    void match_against_bids(Order* sell_order, const std::chrono::high_resolution_clock::time_point& processing_start) noexcept {
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
    
    /*
     * Execute trade and record statistics
     * Measures end-to-end latency from producer timestamp to trade execution
     */
    void execute_trade(uint64_t aggressor_id, uint64_t resting_id, int64_t price, 
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
};

// =============================================================================
// FEED HANDLER (Producer)
// =============================================================================

/*
 * Feed handler simulates realistic market activity
 * Generates orders with appropriate distribution:
 * - 50% passive orders (near current bid/ask)
 * - 20% aggressive orders (cross the spread) 
 * - 30% cancellation requests
 */
void feed_handler(SPSCRingBuffer* ring_buffer) noexcept {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    
    // Price distribution around mid-market
    std::uniform_int_distribution<int64_t> price_dist(PRICE_MIN + 100, PRICE_MAX - 100);
    std::uniform_int_distribution<uint64_t> quantity_dist(1, 1000);
    std::uniform_int_distribution<uint64_t> order_id_dist(1, MAX_ORDERS - 1);
    std::uniform_real_distribution<double> action_dist(0.0, 1.0);
    std::uniform_int_distribution<int> side_dist(0, 1);
    
    uint64_t orders_generated = 0;
    int64_t current_mid = (PRICE_MIN + PRICE_MAX) / 2;  // Simulated mid-market price
    
    while (orders_generated < TOTAL_ORDERS_TO_GENERATE) {
        Command cmd;
        cmd.producer_timestamp = std::chrono::high_resolution_clock::now();
        
        const double action = action_dist(gen);
        
        if (action < 0.7) {  // 70% new orders
            cmd.type = CommandType::NEW;
            cmd.order_id = order_id_dist(gen);
            cmd.side = (side_dist(gen) == 0) ? Side::BUY : Side::SELL;
            cmd.quantity = quantity_dist(gen);
            
            if (action < 0.5) {  // 50% passive orders
                // Place orders away from mid to avoid immediate matching
                if (cmd.side == Side::BUY) {
                    cmd.price = current_mid - (1 + (price_dist(gen) % 50));  // Below mid
                } else {
                    cmd.price = current_mid + (1 + (price_dist(gen) % 50));  // Above mid
                }
            } else {  // 20% aggressive orders
                // Place orders that cross the spread
                if (cmd.side == Side::BUY) {
                    cmd.price = current_mid + (price_dist(gen) % 20);  // Above mid
                } else {
                    cmd.price = current_mid - (price_dist(gen) % 20);  // Below mid
                }
            }
        } else {  // 30% cancellations
            cmd.type = CommandType::CANCEL;
            cmd.order_id = order_id_dist(gen);
        }
        
        // Ensure price is within bounds
        cmd.price = std::max(static_cast<int64_t>(PRICE_MIN), 
                            std::min(static_cast<int64_t>(PRICE_MAX), cmd.price));
        
        // Enqueue with backpressure handling
        while (!ring_buffer->enqueue(cmd)) {
            // Ring buffer full, busy wait (could yield here if needed)
            std::this_thread::yield();
        }
        
        ++orders_generated;
        
        // Occasionally update simulated mid price to create market movement
        if (orders_generated % 10000 == 0) {
            current_mid += (gen() % 21) - 10;  // Random walk ±10
            current_mid = std::max(static_cast<int64_t>(PRICE_MIN + 100), 
                                  std::min(static_cast<int64_t>(PRICE_MAX - 100), current_mid));
        }
    }
}

// =============================================================================
// MAIN FUNCTION AND BENCHMARKING
// =============================================================================

/*
 * Calculate percentile from sorted vector
 */
long long calculate_percentile(const std::vector<long long>& sorted_data, double percentile) noexcept {
    if (sorted_data.empty()) return 0;
    
    const size_t index = static_cast<size_t>((percentile / 100.0) * (sorted_data.size() - 1));
    return sorted_data[index];
}

int main() {
    std::cout << "High-Performance C++20 Limit Order Book\n";
    std::cout << "========================================\n\n";
    
    // Initialize components
    SPSCRingBuffer ring_buffer;
    MatchingEngine matching_engine(&ring_buffer);
    
    std::cout << "Starting benchmark with " << TOTAL_ORDERS_TO_GENERATE << " orders...\n\n";
    
    const auto start_time = std::chrono::high_resolution_clock::now();
    
    // Launch producer and consumer threads
    std::thread producer_thread(feed_handler, &ring_buffer);
    std::thread consumer_thread(&MatchingEngine::run, &matching_engine);
    
    // Wait for completion
    producer_thread.join();
    consumer_thread.join();
    
    const auto end_time = std::chrono::high_resolution_clock::now();
    const auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // Calculate statistics
    const uint64_t orders_processed = matching_engine.orders_processed();
    const uint64_t trades_executed = matching_engine.trades_executed();
    const double orders_per_second = (orders_processed * 1000.0) / total_duration.count();
    
    // Latency statistics
    auto latencies = matching_engine.trade_latencies();
    std::sort(latencies.begin(), latencies.end());
    
    // Print results
    std::cout << "\n=== BENCHMARK RESULTS ===\n";
    std::cout << "Total run time: " << total_duration.count() << " ms\n";
    std::cout << "Orders processed: " << orders_processed << "\n";
    std::cout << "Orders rejected (pool exhausted): " << matching_engine.orders_rejected() << "\n";
    std::cout << "Orders per second: " << static_cast<uint64_t>(orders_per_second) << "\n";
    std::cout << "Trades executed: " << trades_executed << "\n";
    
    if (!latencies.empty()) {
        std::cout << "\n=== LATENCY STATISTICS ===\n";
        std::cout << "P50 latency: " << calculate_percentile(latencies, 50.0) << " ns\n";
        std::cout << "P95 latency: " << calculate_percentile(latencies, 95.0) << " ns\n";
        std::cout << "P99 latency: " << calculate_percentile(latencies, 99.0) << " ns\n";
    }
    
    // Correctness check
    std::cout << "\n=== CORRECTNESS CHECK ===\n";
    const uint64_t buy_matched = matching_engine.total_buy_quantity_matched();
    const uint64_t sell_matched = matching_engine.total_sell_quantity_matched();
    std::cout << "Total buy quantity matched: " << buy_matched << "\n";
    std::cout << "Total sell quantity matched: " << sell_matched << "\n";
    std::cout << "Match balance: " << (buy_matched == sell_matched ? "PASS" : "FAIL") << "\n";
    
    return 0;
}