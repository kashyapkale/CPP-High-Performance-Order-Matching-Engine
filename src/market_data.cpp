#include "market_data.hpp"
#include <iostream>
#include <fstream>
#include <iomanip>

namespace OrderBook {

// Console Market Data Publisher Implementation
void ConsoleMarketDataPublisher::publish_trade(const Trade& trade) {
    std::cout << "TRADE: " << trade.symbol 
              << " price=" << trade.price 
              << " qty=" << trade.quantity
              << " aggressor=" << trade.aggressor_order_id
              << " resting=" << trade.resting_order_id
              << " side=" << (trade.aggressor_side == Side::BUY ? "BUY" : "SELL")
              << std::endl;
}

void ConsoleMarketDataPublisher::publish_level2_snapshot(const Level2Snapshot& snapshot) {
    if (!verbose_) return;
    
    std::cout << "L2_SNAPSHOT: " << snapshot.symbol << std::endl;
    
    // Print asks (highest to lowest)
    std::cout << "  ASKS:" << std::endl;
    for (auto it = snapshot.asks.rbegin(); it != snapshot.asks.rend(); ++it) {
        std::cout << "    " << std::setw(8) << it->price 
                  << " | " << std::setw(8) << it->quantity
                  << " | " << std::setw(4) << it->order_count << std::endl;
    }
    
    std::cout << "  --------" << std::endl;
    
    // Print bids (highest to lowest)  
    std::cout << "  BIDS:" << std::endl;
    for (const auto& level : snapshot.bids) {
        std::cout << "    " << std::setw(8) << level.price 
                  << " | " << std::setw(8) << level.quantity
                  << " | " << std::setw(4) << level.order_count << std::endl;
    }
    std::cout << std::endl;
}

void ConsoleMarketDataPublisher::publish_level2_update(uint32_t instrument_id, const std::string& symbol,
                                                       Side side, int64_t price, uint64_t new_quantity,
                                                       uint32_t new_order_count) {
    if (!verbose_) return;
    
    std::cout << "L2_UPDATE: " << symbol 
              << " " << (side == Side::BUY ? "BID" : "ASK")
              << " price=" << price
              << " qty=" << new_quantity
              << " orders=" << new_order_count << std::endl;
}

// File Market Data Publisher Implementation
void FileMarketDataPublisher::publish_trade(const Trade& trade) {
    std::string filename = base_filename_ + "_trades.csv";
    std::ofstream file(filename, std::ios::app);
    
    if (file.is_open()) {
        // CSV format: timestamp,symbol,price,quantity,aggressor_id,resting_id,aggressor_side
        auto time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            trade.timestamp.time_since_epoch()).count();
        
        file << time_ns << ","
             << trade.symbol << ","
             << trade.price << ","
             << trade.quantity << ","
             << trade.aggressor_order_id << ","
             << trade.resting_order_id << ","
             << (trade.aggressor_side == Side::BUY ? "BUY" : "SELL") << std::endl;
    }
}

void FileMarketDataPublisher::publish_level2_snapshot(const Level2Snapshot& snapshot) {
    std::string filename = base_filename_ + "_l2_" + snapshot.symbol + ".csv";
    std::ofstream file(filename, std::ios::app);
    
    if (file.is_open()) {
        auto time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            snapshot.timestamp.time_since_epoch()).count();
        
        // Write snapshot header
        file << "SNAPSHOT," << time_ns << "," << snapshot.symbol << std::endl;
        
        // Write bids
        for (const auto& level : snapshot.bids) {
            file << "BID," << level.price << "," << level.quantity 
                 << "," << level.order_count << std::endl;
        }
        
        // Write asks
        for (const auto& level : snapshot.asks) {
            file << "ASK," << level.price << "," << level.quantity 
                 << "," << level.order_count << std::endl;
        }
        
        file << "END_SNAPSHOT" << std::endl;
    }
}

void FileMarketDataPublisher::publish_level2_update(uint32_t instrument_id, const std::string& symbol,
                                                    Side side, int64_t price, uint64_t new_quantity,
                                                    uint32_t new_order_count) {
    std::string filename = base_filename_ + "_l2_updates.csv";
    std::ofstream file(filename, std::ios::app);
    
    if (file.is_open()) {
        auto time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        
        file << time_ns << ","
             << symbol << ","
             << (side == Side::BUY ? "BID" : "ASK") << ","
             << price << ","
             << new_quantity << ","
             << new_order_count << std::endl;
    }
}

// Market Data Manager Implementation
void MarketDataManager::add_publisher(std::unique_ptr<MarketDataPublisher> publisher) {
    publishers_.push_back(std::move(publisher));
}

void MarketDataManager::remove_all_publishers() {
    publishers_.clear();
}

void MarketDataManager::publish_trade(const Trade& trade) {
    if (!enabled_) return;
    
    for (auto& publisher : publishers_) {
        publisher->publish_trade(trade);
    }
}

void MarketDataManager::publish_level2_snapshot(const Level2Snapshot& snapshot) {
    if (!enabled_) return;
    
    for (auto& publisher : publishers_) {
        publisher->publish_level2_snapshot(snapshot);
    }
}

void MarketDataManager::publish_level2_update(uint32_t instrument_id, const std::string& symbol,
                                              Side side, int64_t price, uint64_t new_quantity,
                                              uint32_t new_order_count) {
    if (!enabled_) return;
    
    for (auto& publisher : publishers_) {
        publisher->publish_level2_update(instrument_id, symbol, side, price, new_quantity, new_order_count);
    }
}

} // namespace OrderBook