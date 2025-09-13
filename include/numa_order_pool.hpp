#pragma once

#include "types.hpp"
#include "numa_allocator.hpp"
#include <vector>
#include <memory>

namespace OrderBook {

/**
 * NUMA-aware order pool that distributes orders across NUMA nodes
 * for optimal memory locality in multi-socket systems
 */
class NumaOrderPool {
private:
    struct NumaOrderNode {
        numa_vector<Order> orders;
        Order* free_head;
        uint64_t allocated_count;
        int numa_node_id;
        
        NumaOrderNode(NumaAllocator& allocator, int node_id, uint64_t capacity)
            : orders(make_numa_vector<Order>(allocator, node_id)),
              free_head(nullptr), allocated_count(0), numa_node_id(node_id) {
            
            orders.resize(capacity);
            
            // Initialize free list
            for (uint64_t i = 0; i < capacity - 1; ++i) {
                orders[i].next = &orders[i + 1];
            }
            orders[capacity - 1].next = nullptr;
            free_head = &orders[0];
        }
        
        Order* allocate() noexcept {
            if (!free_head) return nullptr;
            
            Order* order = free_head;
            free_head = free_head->next;
            
            // Clear for reuse
            order->next = nullptr;
            order->prev = nullptr;
            ++allocated_count;
            
            return order;
        }
        
        void free(Order* order) noexcept {
            if (!order) return;
            
            order->next = free_head;
            free_head = order;
            --allocated_count;
        }
        
        uint64_t available_count() const noexcept {
            return orders.size() - allocated_count;
        }
    };
    
    NumaAllocator numa_allocator_;
    std::vector<std::unique_ptr<NumaOrderNode>> numa_nodes_;
    uint64_t orders_per_node_;
    
public:
    explicit NumaOrderPool(uint64_t total_orders = MAX_ORDERS) 
        : numa_allocator_(512 * 1024 * 1024), // 512MB per node
          orders_per_node_(total_orders) {
        
        int num_numa_nodes = numa_allocator_.get_numa_node_count();
        orders_per_node_ = total_orders / num_numa_nodes;
        
        // Create order pools for each NUMA node
        for (int node = 0; node < num_numa_nodes; ++node) {
            numa_nodes_.push_back(
                std::make_unique<NumaOrderNode>(numa_allocator_, node, orders_per_node_)
            );
        }
        
        std::cout << "NumaOrderPool initialized with " << num_numa_nodes 
                  << " nodes, " << orders_per_node_ << " orders per node\n";
    }
    
    /**
     * Allocate order from current thread's NUMA node
     */
    Order* allocate() noexcept {
        int preferred_node = numa_allocator_.get_thread_numa_node();
        return allocate_from_node(preferred_node);
    }
    
    /**
     * Allocate order from specific NUMA node
     */
    Order* allocate_from_node(int node_id) noexcept {
        if (node_id < 0 || node_id >= static_cast<int>(numa_nodes_.size())) {
            node_id = 0;  // Fallback to first node
        }
        
        Order* order = numa_nodes_[node_id]->allocate();
        
        // If preferred node is exhausted, try other nodes
        if (!order) {
            for (auto& node : numa_nodes_) {
                if (node->numa_node_id != node_id) {
                    order = node->allocate();
                    if (order) break;
                }
            }
        }
        
        return order;
    }
    
    /**
     * Free order back to its originating NUMA node
     */
    void free(Order* order) noexcept {
        if (!order) return;
        
        // Determine which node this order belongs to by address range
        for (auto& node : numa_nodes_) {
            Order* start = &node->orders[0];
            Order* end = start + node->orders.size();
            
            if (order >= start && order < end) {
                node->free(order);
                return;
            }
        }
        
        // If we can't find the originating node, free to current thread's node
        int current_node = numa_allocator_.get_thread_numa_node();
        if (current_node >= 0 && current_node < static_cast<int>(numa_nodes_.size())) {
            numa_nodes_[current_node]->free(order);
        }
    }
    
    /**
     * Set thread affinity for optimal allocation
     */
    void set_thread_affinity(int numa_node) noexcept {
        numa_allocator_.set_thread_affinity(numa_node);
    }
    
    /**
     * Statistics
     */
    uint64_t total_allocated() const noexcept {
        uint64_t total = 0;
        for (const auto& node : numa_nodes_) {
            total += node->allocated_count;
        }
        return total;
    }
    
    uint64_t total_available() const noexcept {
        uint64_t total = 0;
        for (const auto& node : numa_nodes_) {
            total += node->available_count();
        }
        return total;
    }
    
    uint64_t allocated_on_node(int node_id) const noexcept {
        if (node_id >= 0 && node_id < static_cast<int>(numa_nodes_.size())) {
            return numa_nodes_[node_id]->allocated_count;
        }
        return 0;
    }
    
    uint64_t available_on_node(int node_id) const noexcept {
        if (node_id >= 0 && node_id < static_cast<int>(numa_nodes_.size())) {
            return numa_nodes_[node_id]->available_count();
        }
        return 0;
    }
    
    void print_numa_pool_statistics() const noexcept {
        std::cout << "\n=== NUMA ORDER POOL STATISTICS ===\n";
        std::cout << "NUMA Nodes: " << numa_nodes_.size() << "\n";
        std::cout << "Orders per Node: " << orders_per_node_ << "\n";
        std::cout << "Total Allocated: " << total_allocated() << "\n";
        std::cout << "Total Available: " << total_available() << "\n";
        
        for (size_t i = 0; i < numa_nodes_.size(); ++i) {
            const auto& node = numa_nodes_[i];
            std::cout << "Node " << i << ":\n";
            std::cout << "  Allocated: " << node->allocated_count << "\n";
            std::cout << "  Available: " << node->available_count() << "\n";
            std::cout << "  Utilization: " << std::fixed << std::setprecision(1)
                      << (static_cast<double>(node->allocated_count) / orders_per_node_ * 100.0) 
                      << "%\n";
        }
        
        // Print underlying NUMA allocator stats
        numa_allocator_.print_numa_statistics();
    }
    
    /**
     * Get NUMA topology information
     */
    bool is_numa_available() const noexcept {
        return numa_allocator_.is_numa_available();
    }
    
    int get_numa_node_count() const noexcept {
        return numa_allocator_.get_numa_node_count();
    }
};

} // namespace OrderBook