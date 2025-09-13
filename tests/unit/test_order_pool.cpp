#include <gtest/gtest.h>
#include "order_pool.hpp"
#include "types.hpp"

using namespace OrderBook;

class OrderPoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        pool = std::make_unique<OrderPool>(100);
    }

    std::unique_ptr<OrderPool> pool;
};

TEST_F(OrderPoolTest, InitialState) {
    EXPECT_EQ(pool->allocated_count(), 0);
    EXPECT_EQ(pool->available_count(), 100);
}

TEST_F(OrderPoolTest, AllocateSingleOrder) {
    Order* order = pool->allocate();
    
    ASSERT_NE(order, nullptr);
    EXPECT_EQ(pool->allocated_count(), 1);
    EXPECT_EQ(pool->available_count(), 99);
    
    pool->free(order);
    EXPECT_EQ(pool->allocated_count(), 0);
    EXPECT_EQ(pool->available_count(), 100);
}

TEST_F(OrderPoolTest, AllocateAllOrders) {
    std::vector<Order*> orders;
    
    // Allocate all orders
    for (int i = 0; i < 100; ++i) {
        Order* order = pool->allocate();
        ASSERT_NE(order, nullptr);
        orders.push_back(order);
    }
    
    EXPECT_EQ(pool->allocated_count(), 100);
    EXPECT_EQ(pool->available_count(), 0);
    
    // Should return nullptr when pool is exhausted
    Order* overflow_order = pool->allocate();
    EXPECT_EQ(overflow_order, nullptr);
    
    // Free all orders
    for (Order* order : orders) {
        pool->free(order);
    }
    
    EXPECT_EQ(pool->allocated_count(), 0);
    EXPECT_EQ(pool->available_count(), 100);
}

TEST_F(OrderPoolTest, OrderInitialization) {
    Order* order = pool->allocate();
    ASSERT_NE(order, nullptr);
    
    // Check that order is properly initialized
    EXPECT_EQ(order->next, nullptr);
    EXPECT_EQ(order->prev, nullptr);
    
    pool->free(order);
}

TEST_F(OrderPoolTest, FreeNullPointer) {
    // Should not crash or affect counts
    pool->free(nullptr);
    
    EXPECT_EQ(pool->allocated_count(), 0);
    EXPECT_EQ(pool->available_count(), 100);
}