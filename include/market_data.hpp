#pragma once

#include "types.hpp"
#include <vector>
#include <chrono>
#include <string>

namespace OrderBook {

/**
 * Level 2 market data structures
 */
struct PriceLevelData {
    int64_t price;
    uint64_t quantity;
    uint32_t order_count;
    
    PriceLevelData(int64_t p, uint64_t q, uint32_t count) 
        : price(p), quantity(q), order_count(count) {}
};

struct Level2Snapshot {
    uint32_t instrument_id;
    std::string symbol;
    std::chrono::high_resolution_clock::time_point timestamp;
    std::vector<PriceLevelData> bids;  // Sorted highest to lowest
    std::vector<PriceLevelData> asks;  // Sorted lowest to highest
    
    Level2Snapshot(uint32_t id, const std::string& sym) 
        : instrument_id(id), symbol(sym), 
          timestamp(std::chrono::high_resolution_clock::now()) {
        bids.reserve(20);  // Top 20 levels
        asks.reserve(20);
    }
};

struct Trade {
    uint32_t instrument_id;
    std::string symbol;
    std::chrono::high_resolution_clock::time_point timestamp;
    uint64_t aggressor_order_id;
    uint64_t resting_order_id;
    Side aggressor_side;
    int64_t price;
    uint64_t quantity;
    
    Trade(uint32_t id, const std::string& sym, uint64_t aggr_id, uint64_t rest_id,
          Side side, int64_t p, uint64_t q)
        : instrument_id(id), symbol(sym), 
          timestamp(std::chrono::high_resolution_clock::now()),
          aggressor_order_id(aggr_id), resting_order_id(rest_id),
          aggressor_side(side), price(p), quantity(q) {}
};

/**
 * Market data publisher interface
 */
class MarketDataPublisher {
public:
    virtual ~MarketDataPublisher() = default;
    
    virtual void publish_trade(const Trade& trade) = 0;
    virtual void publish_level2_snapshot(const Level2Snapshot& snapshot) = 0;
    virtual void publish_level2_update(uint32_t instrument_id, const std::string& symbol,
                                     Side side, int64_t price, uint64_t new_quantity,
                                     uint32_t new_order_count) = 0;
};

/**
 * Console-based market data publisher for testing
 */
class ConsoleMarketDataPublisher : public MarketDataPublisher {
private:
    bool verbose_;
    
public:
    explicit ConsoleMarketDataPublisher(bool verbose = false) : verbose_(verbose) {}
    
    void publish_trade(const Trade& trade) override;
    void publish_level2_snapshot(const Level2Snapshot& snapshot) override;
    void publish_level2_update(uint32_t instrument_id, const std::string& symbol,
                              Side side, int64_t price, uint64_t new_quantity,
                              uint32_t new_order_count) override;
};

/**
 * File-based market data publisher for recording
 */
class FileMarketDataPublisher : public MarketDataPublisher {
private:
    std::string base_filename_;
    bool binary_format_;
    
public:
    explicit FileMarketDataPublisher(const std::string& filename, bool binary = false)
        : base_filename_(filename), binary_format_(binary) {}
    
    void publish_trade(const Trade& trade) override;
    void publish_level2_snapshot(const Level2Snapshot& snapshot) override;
    void publish_level2_update(uint32_t instrument_id, const std::string& symbol,
                              Side side, int64_t price, uint64_t new_quantity,
                              uint32_t new_order_count) override;
};

/**
 * Market data manager that handles multiple publishers
 */
class MarketDataManager {
private:
    std::vector<std::unique_ptr<MarketDataPublisher>> publishers_;
    bool enabled_;
    
public:
    MarketDataManager() : enabled_(true) {}
    
    void add_publisher(std::unique_ptr<MarketDataPublisher> publisher);
    void remove_all_publishers();
    
    void enable() { enabled_ = true; }
    void disable() { enabled_ = false; }
    bool is_enabled() const { return enabled_; }
    
    // Publishing methods
    void publish_trade(const Trade& trade);
    void publish_level2_snapshot(const Level2Snapshot& snapshot);
    void publish_level2_update(uint32_t instrument_id, const std::string& symbol,
                              Side side, int64_t price, uint64_t new_quantity,
                              uint32_t new_order_count);
};

} // namespace OrderBook