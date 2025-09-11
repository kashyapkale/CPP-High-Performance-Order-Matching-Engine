# High-Performance C++20 Limit Order Book

A ultra-low latency, lock-free limit order matching engine implementation in modern C++20.

## Architecture Overview 

```
┌─────────────────┐    Lock-Free     ┌──────────────────┐
│   FeedHandler   │ ──── Queue ────→ │ MatchingEngine   │
│   (Producer)    │                  │  (Consumer)      │
└─────────────────┘                  └──────────────────┘
                                              │
                                              ▼
                                     ┌──────────────┐
                                     │  Order Book  │
                                     │ Price Levels │
                                     └──────────────┘
```

## Key Performance Features

- **Zero Dynamic Allocation**: Object pooling eliminates heap allocation on critical path
- **Lock-Free Communication**: SPSC ring buffer with acquire-release memory ordering  
- **O(1) Price Lookup**: Direct-mapped price grid using std::vector
- **Cache-Optimized**: Intrusive linked lists and aligned data structures
- **Single-Writer Design**: Eliminates locks on order book data structures

## Project Structure

```
├── include/                 # Header files
│   ├── types.hpp           # Core data structures
│   ├── order_pool.hpp      # Object pool for orders
│   ├── spsc_ring_buffer.hpp# Lock-free communication
│   ├── book.hpp            # Order book implementation
│   ├── matching_engine.hpp # Core matching logic
│   └── feed_handler.hpp    # Market data simulation
├── src/                    # Implementation files
│   ├── main.cpp           # Entry point and benchmarking
│   ├── types.cpp          # Basic type implementations
│   ├── order_pool.cpp     # Memory pool implementation
│   ├── spsc_ring_buffer.cpp
│   ├── book.cpp
│   ├── matching_engine.cpp
│   └── feed_handler.cpp
├── tests/                  # Unit tests (future)
├── CMakeLists.txt         # CMake build configuration
├── Makefile              # Direct build alternative
└── README.md             # This file
```

## Build Instructions

### Option 1: Using Make (Simple)
```bash
# Release build (optimized)
make release

# Debug build
make debug

# Run the program
make run

# Clean build 
make clean
```

### Option 2: Using CMake (Recommended)
```bash
# Create build directory
mkdir build && cd build

# Configure and build (Release)
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

# Run
./order_matching_engine

# Performance build target
make perf
```

## Performance Characteristics

**Typical Results:**
- **Throughput**: 3+ million orders/second
- **Latency**: P50 < 300ns, P95 < 2μs, P99 < 3μs
- **Trade Rate**: ~28% (realistic market simulation)
- **Memory**: Zero allocation on critical path

## Configuration

Key parameters in `include/types.hpp`:
```cpp
constexpr uint64_t PRICE_MIN = 0;
constexpr uint64_t PRICE_MAX = 10000;        // Price range
constexpr uint64_t MAX_ORDERS = 1000000;     // Object pool size
constexpr uint64_t RING_BUFFER_SIZE = 1<<20; // Communication buffer
constexpr uint64_t TOTAL_ORDERS_TO_GENERATE = 20000000; // Test load
```

## Market Data Simulation

The feed handler generates realistic order flow:
- **50%** Passive orders (build liquidity, placed away from mid)  
- **20%** Aggressive orders (consume liquidity, cross spread)
- **30%** Cancellation requests (order management)

## Design Principles

1. **Single Writer**: Consumer thread owns all order book data
2. **Memory Locality**: Intrusive data structures minimize pointer chasing
3. **Bounded Resources**: Fixed-size pools eliminate allocation uncertainty
4. **Price-Time Priority**: Strict FIFO within price levels
5. **Lock-Free**: Producer-consumer communication without locks

## Testing

```bash
# Build and run basic functionality test
make && ./order_matching_engine

# Performance comparison
time ./order_matching_engine | tail -10
```

## Future Enhancements

- [ ] Unit test suite
- [ ] Multiple instrument support  
- [ ] Market data output (Level 2)
- [ ] Order types (IOC, FOK, etc.)
- [ ] Risk checks and limits
- [ ] NUMA-aware memory allocation
