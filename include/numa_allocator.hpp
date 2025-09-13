#pragma once

#include <cstddef>
#include <memory>
#include <vector>
#include <atomic>
#include <thread>
#include <numa.h>

#ifdef __APPLE__
// macOS doesn't have NUMA support, provide stub implementation
namespace NumaStub {
    inline int numa_available() { return -1; }
    inline int numa_max_node() { return 0; }
    inline void* numa_alloc_onnode(size_t size, int node) { return malloc(size); }
    inline void numa_free(void* ptr, size_t size) { free(ptr); }
    inline int numa_node_of_cpu(int cpu) { return 0; }
    inline void numa_set_strict(int flag) {}
    inline void numa_set_bind_policy(int flag) {}
}
#define numa_available NumaStub::numa_available
#define numa_max_node NumaStub::numa_max_node  
#define numa_alloc_onnode NumaStub::numa_alloc_onnode
#define numa_free NumaStub::numa_free
#define numa_node_of_cpu NumaStub::numa_node_of_cpu
#define numa_set_strict NumaStub::numa_set_strict
#define numa_set_bind_policy NumaStub::numa_set_bind_policy
#endif

namespace OrderBook {

/**
 * NUMA-aware memory allocator for high-performance trading systems
 * 
 * Key concepts:
 * - Thread-local allocation to minimize cross-NUMA memory access
 * - Pre-allocated memory pools per NUMA node
 * - Processor affinity awareness for optimal placement
 */
class NumaAllocator {
private:
    struct NumaNode {
        int node_id;
        void* memory_pool;
        size_t pool_size;
        std::atomic<size_t> allocated_bytes{0};
        std::atomic<size_t> allocation_count{0};
        
        NumaNode(int id, size_t size) : node_id(id), pool_size(size) {
            memory_pool = numa_alloc_onnode(size, id);
            if (!memory_pool) {
                memory_pool = malloc(size);  // Fallback to regular malloc
            }
        }
        
        ~NumaNode() {
            if (memory_pool) {
                numa_free(memory_pool, pool_size);
            }
        }
    };
    
    std::vector<std::unique_ptr<NumaNode>> numa_nodes_;
    bool numa_available_;
    int max_numa_nodes_;
    
    // Thread-local storage for NUMA node affinity
    static thread_local int thread_numa_node_;
    
public:
    /**
     * Initialize NUMA allocator with specified pool size per node
     */
    explicit NumaAllocator(size_t pool_size_per_node = 1024 * 1024 * 1024) // 1GB default
        : numa_available_(numa_available() >= 0), max_numa_nodes_(0) {
        
        if (numa_available_) {
            max_numa_nodes_ = numa_max_node() + 1;
            numa_set_strict(1);  // Enforce strict NUMA policy
            
            // Create memory pools for each NUMA node
            for (int node = 0; node < max_numa_nodes_; ++node) {
                numa_nodes_.push_back(std::make_unique<NumaNode>(node, pool_size_per_node));
            }
        } else {
            // Fallback: single "node" using regular allocation
            max_numa_nodes_ = 1;
            numa_nodes_.push_back(std::make_unique<NumaNode>(0, pool_size_per_node));
        }
    }
    
    /**
     * Set thread affinity to specific NUMA node
     */
    void set_thread_affinity(int numa_node) noexcept {
        if (numa_node >= 0 && numa_node < max_numa_nodes_) {
            thread_numa_node_ = numa_node;
        }
    }
    
    /**
     * Get current thread's NUMA node (auto-detected or manually set)
     */
    int get_thread_numa_node() const noexcept {
        if (thread_numa_node_ >= 0) {
            return thread_numa_node_;
        }
        
        if (numa_available_) {
            // Auto-detect based on current CPU
            int cpu = sched_getcpu();
            if (cpu >= 0) {
                return numa_node_of_cpu(cpu);
            }
        }
        
        return 0;  // Default to node 0
    }
    
    /**
     * Allocate memory on current thread's NUMA node
     */
    void* allocate(size_t size, size_t alignment = alignof(std::max_align_t)) noexcept {
        int node = get_thread_numa_node();
        return allocate_on_node(size, node, alignment);
    }
    
    /**
     * Allocate memory on specific NUMA node
     */
    void* allocate_on_node(size_t size, int node, size_t alignment = alignof(std::max_align_t)) noexcept {
        if (node < 0 || node >= max_numa_nodes_) {
            node = 0;
        }
        
        NumaNode* numa_node = numa_nodes_[node].get();
        
        // For now, use simple system allocation
        // In production, this would use the pre-allocated pool
        void* ptr = numa_available_ ? numa_alloc_onnode(size, node) : malloc(size);
        
        if (ptr) {
            numa_node->allocated_bytes += size;
            numa_node->allocation_count++;
        }
        
        return ptr;
    }
    
    /**
     * Free NUMA-allocated memory
     */
    void deallocate(void* ptr, size_t size, int node = -1) noexcept {
        if (!ptr) return;
        
        if (node < 0) {
            node = get_thread_numa_node();
        }
        
        if (node >= 0 && node < max_numa_nodes_) {
            NumaNode* numa_node = numa_nodes_[node].get();
            numa_node->allocated_bytes -= size;
            numa_node->allocation_count--;
        }
        
        numa_available_ ? numa_free(ptr, size) : free(ptr);
    }
    
    /**
     * NUMA topology information
     */
    bool is_numa_available() const noexcept { return numa_available_; }
    int get_numa_node_count() const noexcept { return max_numa_nodes_; }
    
    /**
     * Memory statistics per NUMA node
     */
    struct NumaStats {
        int node_id;
        size_t allocated_bytes;
        size_t allocation_count;
        size_t pool_size;
    };
    
    std::vector<NumaStats> get_numa_statistics() const noexcept {
        std::vector<NumaStats> stats;
        stats.reserve(numa_nodes_.size());
        
        for (const auto& node : numa_nodes_) {
            stats.push_back({
                node->node_id,
                node->allocated_bytes.load(),
                node->allocation_count.load(),
                node->pool_size
            });
        }
        
        return stats;
    }
    
    void print_numa_statistics() const noexcept {
        std::cout << "\n=== NUMA MEMORY STATISTICS ===\n";
        std::cout << "NUMA Available: " << (numa_available_ ? "YES" : "NO") << "\n";
        std::cout << "NUMA Nodes: " << max_numa_nodes_ << "\n";
        
        auto stats = get_numa_statistics();
        for (const auto& stat : stats) {
            std::cout << "Node " << stat.node_id << ":\n";
            std::cout << "  Allocated: " << stat.allocated_bytes / 1024 / 1024 << " MB\n";
            std::cout << "  Allocations: " << stat.allocation_count << "\n";
            std::cout << "  Pool Size: " << stat.pool_size / 1024 / 1024 << " MB\n";
        }
        std::cout << "\n";
    }
};

// Thread-local storage definition
thread_local int NumaAllocator::thread_numa_node_ = -1;

/**
 * NUMA-aware STL allocator adapter
 */
template<typename T>
class NumaStlAllocator {
private:
    NumaAllocator* numa_allocator_;
    int preferred_node_;
    
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    
    template<typename U>
    struct rebind {
        using other = NumaStlAllocator<U>;
    };
    
    explicit NumaStlAllocator(NumaAllocator* allocator, int node = -1) noexcept
        : numa_allocator_(allocator), preferred_node_(node) {}
    
    template<typename U>
    NumaStlAllocator(const NumaStlAllocator<U>& other) noexcept
        : numa_allocator_(other.numa_allocator_), preferred_node_(other.preferred_node_) {}
    
    pointer allocate(size_type n) {
        size_type size = n * sizeof(T);
        void* ptr = (preferred_node_ >= 0) ? 
            numa_allocator_->allocate_on_node(size, preferred_node_) :
            numa_allocator_->allocate(size);
        
        if (!ptr) {
            throw std::bad_alloc();
        }
        
        return static_cast<pointer>(ptr);
    }
    
    void deallocate(pointer ptr, size_type n) noexcept {
        if (ptr) {
            size_type size = n * sizeof(T);
            numa_allocator_->deallocate(ptr, size, preferred_node_);
        }
    }
    
    template<typename U>
    bool operator==(const NumaStlAllocator<U>& other) const noexcept {
        return numa_allocator_ == other.numa_allocator_ && 
               preferred_node_ == other.preferred_node_;
    }
    
    template<typename U>
    bool operator!=(const NumaStlAllocator<U>& other) const noexcept {
        return !(*this == other);
    }
    
    // Allow access to private members for rebind
    template<typename U> friend class NumaStlAllocator;
    NumaAllocator* numa_allocator_;
};

/**
 * NUMA-aware vector typedef for common use
 */
template<typename T>
using numa_vector = std::vector<T, NumaStlAllocator<T>>;

/**
 * Helper function to create NUMA-aware containers
 */
template<typename T>
numa_vector<T> make_numa_vector(NumaAllocator& allocator, int node = -1) {
    return numa_vector<T>(NumaStlAllocator<T>(&allocator, node));
}

} // namespace OrderBook