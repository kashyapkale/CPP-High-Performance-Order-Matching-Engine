#include <gtest/gtest.h>
#include "spsc_ring_buffer.hpp"
#include "types.hpp"
#include <thread>
#include <vector>
#include <chrono>

using namespace OrderBook;

class SPSCRingBufferTest : public ::testing::Test {
protected:
    void SetUp() override {
        buffer = std::make_unique<SPSCRingBuffer>();
    }

    Command createTestCommand(uint64_t id) {
        Command cmd;
        cmd.type = CommandType::NEW;
        cmd.order_id = id;
        cmd.side = Side::BUY;
        cmd.price = 5000;
        cmd.quantity = 100;
        cmd.producer_timestamp = std::chrono::high_resolution_clock::now();
        return cmd;
    }

    std::unique_ptr<SPSCRingBuffer> buffer;
};

TEST_F(SPSCRingBufferTest, EmptyBufferDequeue) {
    Command cmd;
    EXPECT_FALSE(buffer->dequeue(cmd));
}

TEST_F(SPSCRingBufferTest, SingleEnqueueDequeue) {
    Command original = createTestCommand(123);
    
    EXPECT_TRUE(buffer->enqueue(original));
    
    Command dequeued;
    EXPECT_TRUE(buffer->dequeue(dequeued));
    
    EXPECT_EQ(dequeued.order_id, original.order_id);
    EXPECT_EQ(dequeued.type, original.type);
    EXPECT_EQ(dequeued.side, original.side);
    EXPECT_EQ(dequeued.price, original.price);
    EXPECT_EQ(dequeued.quantity, original.quantity);
}

TEST_F(SPSCRingBufferTest, MultipleEnqueueDequeue) {
    std::vector<Command> commands;
    
    // Enqueue multiple commands
    for (uint64_t i = 0; i < 10; ++i) {
        Command cmd = createTestCommand(i);
        commands.push_back(cmd);
        EXPECT_TRUE(buffer->enqueue(cmd));
    }
    
    // Dequeue and verify order
    for (uint64_t i = 0; i < 10; ++i) {
        Command dequeued;
        EXPECT_TRUE(buffer->dequeue(dequeued));
        EXPECT_EQ(dequeued.order_id, i);
    }
    
    // Buffer should be empty now
    Command empty;
    EXPECT_FALSE(buffer->dequeue(empty));
}

TEST_F(SPSCRingBufferTest, BufferCapacity) {
    // Fill buffer to near capacity (leave some space for the full detection)
    const uint64_t commands_to_add = RING_BUFFER_SIZE - 100;
    
    for (uint64_t i = 0; i < commands_to_add; ++i) {
        Command cmd = createTestCommand(i);
        EXPECT_TRUE(buffer->enqueue(cmd));
    }
    
    // Should still be able to add a few more
    Command cmd = createTestCommand(999);
    EXPECT_TRUE(buffer->enqueue(cmd));
}

TEST_F(SPSCRingBufferTest, ConcurrentProducerConsumer) {
    const uint64_t num_messages = 10000;
    std::atomic<uint64_t> consumed_count{0};
    std::atomic<bool> producer_done{false};
    
    // Producer thread
    std::thread producer([this, num_messages, &producer_done]() {
        for (uint64_t i = 0; i < num_messages; ++i) {
            Command cmd = createTestCommand(i);
            while (!buffer->enqueue(cmd)) {
                // Busy wait if buffer is full
                std::this_thread::yield();
            }
        }
        producer_done = true;
    });
    
    // Consumer thread
    std::thread consumer([this, &consumed_count, &producer_done]() {
        Command cmd;
        while (!producer_done || buffer->dequeue(cmd)) {
            if (buffer->dequeue(cmd)) {
                consumed_count++;
            } else {
                std::this_thread::yield();
            }
        }
    });
    
    producer.join();
    consumer.join();
    
    EXPECT_EQ(consumed_count.load(), num_messages);
}

TEST_F(SPSCRingBufferTest, OrderPreservation) {
    const uint64_t num_messages = 1000;
    
    // Enqueue messages
    for (uint64_t i = 0; i < num_messages; ++i) {
        Command cmd = createTestCommand(i);
        EXPECT_TRUE(buffer->enqueue(cmd));
    }
    
    // Dequeue and verify order is preserved
    for (uint64_t i = 0; i < num_messages; ++i) {
        Command cmd;
        EXPECT_TRUE(buffer->dequeue(cmd));
        EXPECT_EQ(cmd.order_id, i);
    }
}