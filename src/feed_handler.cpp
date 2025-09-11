#include "feed_handler.hpp"
#include "types.hpp"
#include <random>
#include <thread>
#include <algorithm>

namespace OrderBook {

void FeedHandler::run(SPSCRingBuffer* ring_buffer) noexcept {
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

} // namespace OrderBook