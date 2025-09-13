#pragma once

#include "types.hpp"
#include <unordered_map>
#include <atomic>
#include <cstdint>

namespace OrderBook {

/**
 * Risk limits configuration
 */
struct RiskLimits {
    // Position limits
    int64_t max_position = 1000000;          // Maximum net position
    uint64_t max_order_size = 100000;        // Maximum single order size
    uint64_t max_order_value = 10000000;     // Maximum single order value (price * quantity)
    
    // Rate limits
    uint32_t max_orders_per_second = 1000;   // Maximum orders per second
    uint32_t max_cancels_per_second = 500;   // Maximum cancels per second
    
    // Exposure limits
    uint64_t max_gross_exposure = 5000000;   // Maximum total exposure (long + short)
    uint64_t max_daily_volume = 50000000;    // Maximum daily trading volume
    
    // Price limits
    double max_price_deviation = 0.10;       // 10% from reference price
    
    RiskLimits() = default;
};

/**
 * Trading session and account state
 */
struct TradingAccount {
    std::string account_id;
    int64_t net_position = 0;                // Current net position (positive = long)
    uint64_t gross_exposure = 0;             // Total exposure (|long| + |short|)
    uint64_t daily_volume = 0;               // Total volume traded today
    uint64_t daily_trade_count = 0;          // Number of trades today
    
    // Rate limiting
    uint32_t orders_this_second = 0;
    uint32_t cancels_this_second = 0;
    std::chrono::high_resolution_clock::time_point last_second_reset;
    
    RiskLimits limits;
    bool enabled = true;
    
    explicit TradingAccount(const std::string& id) 
        : account_id(id), last_second_reset(std::chrono::high_resolution_clock::now()) {}
};

/**
 * Risk check results
 */
enum class RiskCheckResult {
    ACCEPTED,
    REJECTED_POSITION_LIMIT,
    REJECTED_ORDER_SIZE,
    REJECTED_ORDER_VALUE,
    REJECTED_RATE_LIMIT,
    REJECTED_EXPOSURE_LIMIT,
    REJECTED_DAILY_VOLUME,
    REJECTED_PRICE_DEVIATION,
    REJECTED_ACCOUNT_DISABLED,
    REJECTED_UNKNOWN_ACCOUNT
};

/**
 * Risk manager for pre-trade and post-trade risk checks
 */
class RiskManager {
private:
    std::unordered_map<std::string, TradingAccount> accounts_;
    std::atomic<bool> enabled_{true};
    int64_t reference_price_ = 5000;  // Reference price for deviation checks
    
    // Risk statistics
    std::atomic<uint64_t> total_orders_checked_{0};
    std::atomic<uint64_t> total_orders_rejected_{0};
    std::array<std::atomic<uint64_t>, 10> rejection_counts_{};  // Per rejection reason
    
public:
    RiskManager() = default;
    
    /**
     * Account management
     */
    bool add_account(const std::string& account_id, const RiskLimits& limits);
    bool remove_account(const std::string& account_id);
    bool update_limits(const std::string& account_id, const RiskLimits& limits);
    bool enable_account(const std::string& account_id, bool enabled);
    
    /**
     * Risk checking
     */
    RiskCheckResult check_new_order(const std::string& account_id, const Command& cmd) noexcept;
    RiskCheckResult check_cancel_order(const std::string& account_id, uint64_t order_id) noexcept;
    
    /**
     * Post-trade updates
     */
    void update_position(const std::string& account_id, Side side, uint64_t quantity, int64_t price) noexcept;
    void update_daily_volume(const std::string& account_id, uint64_t volume) noexcept;
    
    /**
     * Reference price management for deviation checks
     */
    void set_reference_price(int64_t price) noexcept { reference_price_ = price; }
    int64_t get_reference_price() const noexcept { return reference_price_; }
    
    /**
     * Risk manager control
     */
    void enable() noexcept { enabled_ = true; }
    void disable() noexcept { enabled_ = false; }
    bool is_enabled() const noexcept { return enabled_; }
    
    /**
     * Statistics
     */
    uint64_t total_orders_checked() const noexcept { return total_orders_checked_; }
    uint64_t total_orders_rejected() const noexcept { return total_orders_rejected_; }
    uint64_t rejection_count(RiskCheckResult reason) const noexcept;
    double rejection_rate() const noexcept;
    
    void print_risk_statistics() const noexcept;
    void reset_daily_limits() noexcept;  // Call at start of each trading day
    
    /**
     * Account queries
     */
    const TradingAccount* get_account(const std::string& account_id) const noexcept;
    std::vector<std::string> get_all_account_ids() const noexcept;
    
private:
    void update_rate_limits(TradingAccount& account) noexcept;
    bool check_price_deviation(int64_t order_price, double max_deviation) const noexcept;
    const char* risk_result_to_string(RiskCheckResult result) const noexcept;
};

/**
 * Enhanced command with account information
 */
struct RiskManagedCommand {
    std::string account_id;
    Command base_command;
    
    RiskManagedCommand(const std::string& account, const Command& cmd) 
        : account_id(account), base_command(cmd) {}
};

} // namespace OrderBook