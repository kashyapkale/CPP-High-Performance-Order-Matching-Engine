#include "matching_engine.hpp"
#include "feed_handler.hpp"
#include "spsc_ring_buffer.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <algorithm>

using namespace OrderBook;

/**
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
    std::thread producer_thread(FeedHandler::run, &ring_buffer);
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