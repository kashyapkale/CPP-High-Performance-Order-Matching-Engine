#include <gtest/gtest.h>
#include "types.hpp"
#include <memory>

using namespace OrderBook;

class PriceLevelTest : public ::testing::Test {
protected:
    void SetUp() override {
        level = std::make_unique<PriceLevel>();
        
        // Create test orders
        for (int i = 0; i < 3; ++i) {
            auto order = std::make_unique<Order>();
            order->order_id = i + 1;
            order->quantity = (i + 1) * 100;
            order->side = Side::BUY;
            order->price = 5000;
            orders.push_back(std::move(order));
        }
    }

    std::unique_ptr<PriceLevel> level;
    std::vector<std::unique_ptr<Order>> orders;
};

TEST_F(PriceLevelTest, InitialState) {
    EXPECT_TRUE(level->empty());
    EXPECT_EQ(level->total_volume, 0);
    EXPECT_EQ(level->head, nullptr);
    EXPECT_EQ(level->tail, nullptr);
}

TEST_F(PriceLevelTest, AddSingleOrder) {
    Order* order = orders[0].get();
    level->add_order(order);
    
    EXPECT_FALSE(level->empty());
    EXPECT_EQ(level->total_volume, 100);
    EXPECT_EQ(level->head, order);
    EXPECT_EQ(level->tail, order);
    EXPECT_EQ(order->next, nullptr);
    EXPECT_EQ(order->prev, nullptr);
}

TEST_F(PriceLevelTest, AddMultipleOrders) {
    // Add orders in sequence
    for (int i = 0; i < 3; ++i) {
        level->add_order(orders[i].get());
    }
    
    EXPECT_FALSE(level->empty());
    EXPECT_EQ(level->total_volume, 600); // 100 + 200 + 300
    
    // Check FIFO ordering
    EXPECT_EQ(level->head, orders[0].get());
    EXPECT_EQ(level->tail, orders[2].get());
    
    // Check linked list structure
    EXPECT_EQ(orders[0]->next, orders[1].get());
    EXPECT_EQ(orders[1]->prev, orders[0].get());
    EXPECT_EQ(orders[1]->next, orders[2].get());
    EXPECT_EQ(orders[2]->prev, orders[1].get());
}

TEST_F(PriceLevelTest, RemoveMiddleOrder) {
    // Add all orders
    for (int i = 0; i < 3; ++i) {
        level->add_order(orders[i].get());
    }
    
    // Remove middle order
    level->remove_order(orders[1].get());
    
    EXPECT_EQ(level->total_volume, 400); // 100 + 300
    EXPECT_EQ(orders[0]->next, orders[2].get());
    EXPECT_EQ(orders[2]->prev, orders[0].get());
}

TEST_F(PriceLevelTest, RemoveHeadOrder) {
    // Add all orders
    for (int i = 0; i < 3; ++i) {
        level->add_order(orders[i].get());
    }
    
    // Remove head
    level->remove_order(orders[0].get());
    
    EXPECT_EQ(level->total_volume, 500); // 200 + 300
    EXPECT_EQ(level->head, orders[1].get());
    EXPECT_EQ(orders[1]->prev, nullptr);
}

TEST_F(PriceLevelTest, RemoveTailOrder) {
    // Add all orders
    for (int i = 0; i < 3; ++i) {
        level->add_order(orders[i].get());
    }
    
    // Remove tail
    level->remove_order(orders[2].get());
    
    EXPECT_EQ(level->total_volume, 300); // 100 + 200
    EXPECT_EQ(level->tail, orders[1].get());
    EXPECT_EQ(orders[1]->next, nullptr);
}

TEST_F(PriceLevelTest, RemoveAllOrders) {
    // Add all orders
    for (int i = 0; i < 3; ++i) {
        level->add_order(orders[i].get());
    }
    
    // Remove all orders
    level->remove_order(orders[1].get());
    level->remove_order(orders[0].get());
    level->remove_order(orders[2].get());
    
    EXPECT_TRUE(level->empty());
    EXPECT_EQ(level->total_volume, 0);
    EXPECT_EQ(level->head, nullptr);
    EXPECT_EQ(level->tail, nullptr);
}