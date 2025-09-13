#include "risk_manager.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>

namespace OrderBook {

bool RiskManager::add_account(const std::string& account_id, const RiskLimits& limits) {
    auto [it, inserted] = accounts_.try_emplace(account_id, account_id);
    if (inserted) {
        it->second.limits = limits;
        return true;
    }
    return false; // Account already exists
}

bool RiskManager::remove_account(const std::string& account_id) {
    return accounts_.erase(account_id) > 0;
}

bool RiskManager::update_limits(const std::string& account_id, const RiskLimits& limits) {
    auto it = accounts_.find(account_id);
    if (it != accounts_.end()) {
        it->second.limits = limits;
        return true;
    }
    return false;
}

bool RiskManager::enable_account(const std::string& account_id, bool enabled) {
    auto it = accounts_.find(account_id);
    if (it != accounts_.end()) {
        it->second.enabled = enabled;
        return true;
    }
    return false;
}

RiskCheckResult RiskManager::check_new_order(const std::string& account_id, const Command& cmd) noexcept {
    total_orders_checked_++;
    
    if (!enabled_) {
        return RiskCheckResult::ACCEPTED;
    }
    
    auto it = accounts_.find(account_id);
    if (it == accounts_.end()) {
        total_orders_rejected_++;
        rejection_counts_[static_cast<size_t>(RiskCheckResult::REJECTED_UNKNOWN_ACCOUNT)]++;
        return RiskCheckResult::REJECTED_UNKNOWN_ACCOUNT;
    }
    
    TradingAccount& account = it->second;
    
    if (!account.enabled) {
        total_orders_rejected_++;
        rejection_counts_[static_cast<size_t>(RiskCheckResult::REJECTED_ACCOUNT_DISABLED)]++;
        return RiskCheckResult::REJECTED_ACCOUNT_DISABLED;
    }
    
    update_rate_limits(account);
    
    // Check order size limit
    if (cmd.quantity > account.limits.max_order_size) {
        total_orders_rejected_++;
        rejection_counts_[static_cast<size_t>(RiskCheckResult::REJECTED_ORDER_SIZE)]++;
        return RiskCheckResult::REJECTED_ORDER_SIZE;
    }
    
    // Check order value limit
    uint64_t order_value = static_cast<uint64_t>(cmd.price) * cmd.quantity;
    if (order_value > account.limits.max_order_value) {
        total_orders_rejected_++;
        rejection_counts_[static_cast<size_t>(RiskCheckResult::REJECTED_ORDER_VALUE)]++;
        return RiskCheckResult::REJECTED_ORDER_VALUE;
    }
    
    // Check rate limits
    if (account.orders_this_second >= account.limits.max_orders_per_second) {
        total_orders_rejected_++;
        rejection_counts_[static_cast<size_t>(RiskCheckResult::REJECTED_RATE_LIMIT)]++;
        return RiskCheckResult::REJECTED_RATE_LIMIT;
    }
    
    // Check position limits
    int64_t position_change = (cmd.side == Side::BUY) ? 
        static_cast<int64_t>(cmd.quantity) : -static_cast<int64_t>(cmd.quantity);
    int64_t new_position = account.net_position + position_change;
    
    if (std::abs(new_position) > account.limits.max_position) {
        total_orders_rejected_++;
        rejection_counts_[static_cast<size_t>(RiskCheckResult::REJECTED_POSITION_LIMIT)]++;
        return RiskCheckResult::REJECTED_POSITION_LIMIT;
    }
    
    // Check gross exposure limits
    uint64_t new_gross_exposure = account.gross_exposure + cmd.quantity;
    if (new_gross_exposure > account.limits.max_gross_exposure) {
        total_orders_rejected_++;
        rejection_counts_[static_cast<size_t>(RiskCheckResult::REJECTED_EXPOSURE_LIMIT)]++;
        return RiskCheckResult::REJECTED_EXPOSURE_LIMIT;
    }
    
    // Check daily volume limit
    if (account.daily_volume + cmd.quantity > account.limits.max_daily_volume) {
        total_orders_rejected_++;
        rejection_counts_[static_cast<size_t>(RiskCheckResult::REJECTED_DAILY_VOLUME)]++;
        return RiskCheckResult::REJECTED_DAILY_VOLUME;
    }
    
    // Check price deviation
    if (!check_price_deviation(cmd.price, account.limits.max_price_deviation)) {
        total_orders_rejected_++;
        rejection_counts_[static_cast<size_t>(RiskCheckResult::REJECTED_PRICE_DEVIATION)]++;
        return RiskCheckResult::REJECTED_PRICE_DEVIATION;
    }
    
    // Update rate limiting counter
    account.orders_this_second++;
    
    return RiskCheckResult::ACCEPTED;
}

RiskCheckResult RiskManager::check_cancel_order(const std::string& account_id, uint64_t order_id) noexcept {
    if (!enabled_) {
        return RiskCheckResult::ACCEPTED;
    }
    
    auto it = accounts_.find(account_id);
    if (it == accounts_.end()) {
        return RiskCheckResult::REJECTED_UNKNOWN_ACCOUNT;
    }
    
    TradingAccount& account = it->second;
    
    if (!account.enabled) {
        return RiskCheckResult::REJECTED_ACCOUNT_DISABLED;
    }
    
    update_rate_limits(account);
    
    // Check cancel rate limits
    if (account.cancels_this_second >= account.limits.max_cancels_per_second) {
        return RiskCheckResult::REJECTED_RATE_LIMIT;
    }
    
    account.cancels_this_second++;
    return RiskCheckResult::ACCEPTED;
}

void RiskManager::update_position(const std::string& account_id, Side side, uint64_t quantity, int64_t price) noexcept {
    auto it = accounts_.find(account_id);
    if (it == accounts_.end()) return;
    
    TradingAccount& account = it->second;
    
    int64_t position_change = (side == Side::BUY) ? 
        static_cast<int64_t>(quantity) : -static_cast<int64_t>(quantity);
    
    account.net_position += position_change;
    account.gross_exposure += quantity;
    account.daily_volume += quantity;
    account.daily_trade_count++;
}

void RiskManager::update_daily_volume(const std::string& account_id, uint64_t volume) noexcept {
    auto it = accounts_.find(account_id);
    if (it != accounts_.end()) {
        it->second.daily_volume += volume;
    }
}

uint64_t RiskManager::rejection_count(RiskCheckResult reason) const noexcept {
    size_t index = static_cast<size_t>(reason);
    if (index < rejection_counts_.size()) {
        return rejection_counts_[index];
    }
    return 0;
}

double RiskManager::rejection_rate() const noexcept {
    uint64_t total = total_orders_checked_;
    if (total == 0) return 0.0;
    return static_cast<double>(total_orders_rejected_) / total * 100.0;
}

void RiskManager::print_risk_statistics() const noexcept {
    std::cout << "\n=== RISK MANAGEMENT STATISTICS ===\n";
    std::cout << "Enabled: " << (enabled_ ? "YES" : "NO") << "\n";
    std::cout << "Total Orders Checked: " << total_orders_checked_ << "\n";
    std::cout << "Total Orders Rejected: " << total_orders_rejected_ << "\n";
    std::cout << "Rejection Rate: " << std::fixed << std::setprecision(2) 
              << rejection_rate() << "%\n";
    
    std::cout << "\nRejection Reasons:\n";
    const char* reasons[] = {
        "ACCEPTED", "POSITION_LIMIT", "ORDER_SIZE", "ORDER_VALUE", 
        "RATE_LIMIT", "EXPOSURE_LIMIT", "DAILY_VOLUME", "PRICE_DEVIATION",
        "ACCOUNT_DISABLED", "UNKNOWN_ACCOUNT"
    };
    
    for (size_t i = 1; i < rejection_counts_.size(); ++i) { // Skip ACCEPTED
        uint64_t count = rejection_counts_[i];
        if (count > 0) {
            std::cout << "  " << reasons[i] << ": " << count << "\n";
        }
    }
    
    std::cout << "\nAccount Summary:\n";
    for (const auto& [account_id, account] : accounts_) {
        std::cout << "  Account: " << account_id << "\n";
        std::cout << "    Enabled: " << (account.enabled ? "YES" : "NO") << "\n";
        std::cout << "    Net Position: " << account.net_position << "\n";
        std::cout << "    Gross Exposure: " << account.gross_exposure << "\n";
        std::cout << "    Daily Volume: " << account.daily_volume << "\n";
        std::cout << "    Daily Trades: " << account.daily_trade_count << "\n";
    }
    std::cout << "\n";
}

void RiskManager::reset_daily_limits() noexcept {
    for (auto& [account_id, account] : accounts_) {
        account.daily_volume = 0;
        account.daily_trade_count = 0;
    }
}

const TradingAccount* RiskManager::get_account(const std::string& account_id) const noexcept {
    auto it = accounts_.find(account_id);
    return (it != accounts_.end()) ? &it->second : nullptr;
}

std::vector<std::string> RiskManager::get_all_account_ids() const noexcept {
    std::vector<std::string> account_ids;
    account_ids.reserve(accounts_.size());
    
    for (const auto& [account_id, account] : accounts_) {
        account_ids.push_back(account_id);
    }
    
    return account_ids;
}

void RiskManager::update_rate_limits(TradingAccount& account) noexcept {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - account.last_second_reset);
    
    if (duration.count() >= 1) {
        account.orders_this_second = 0;
        account.cancels_this_second = 0;
        account.last_second_reset = now;
    }
}

bool RiskManager::check_price_deviation(int64_t order_price, double max_deviation) const noexcept {
    if (reference_price_ <= 0) return true; // No reference price set
    
    double deviation = std::abs(static_cast<double>(order_price - reference_price_)) / reference_price_;
    return deviation <= max_deviation;
}

const char* RiskManager::risk_result_to_string(RiskCheckResult result) const noexcept {
    switch (result) {
        case RiskCheckResult::ACCEPTED: return "ACCEPTED";
        case RiskCheckResult::REJECTED_POSITION_LIMIT: return "POSITION_LIMIT";
        case RiskCheckResult::REJECTED_ORDER_SIZE: return "ORDER_SIZE";
        case RiskCheckResult::REJECTED_ORDER_VALUE: return "ORDER_VALUE";
        case RiskCheckResult::REJECTED_RATE_LIMIT: return "RATE_LIMIT";
        case RiskCheckResult::REJECTED_EXPOSURE_LIMIT: return "EXPOSURE_LIMIT";
        case RiskCheckResult::REJECTED_DAILY_VOLUME: return "DAILY_VOLUME";
        case RiskCheckResult::REJECTED_PRICE_DEVIATION: return "PRICE_DEVIATION";
        case RiskCheckResult::REJECTED_ACCOUNT_DISABLED: return "ACCOUNT_DISABLED";
        case RiskCheckResult::REJECTED_UNKNOWN_ACCOUNT: return "UNKNOWN_ACCOUNT";
        default: return "UNKNOWN";
    }
}

} // namespace OrderBook