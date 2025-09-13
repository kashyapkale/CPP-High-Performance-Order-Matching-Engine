# Test Configuration
cmake_minimum_required(VERSION 3.16)

# Find Google Test
find_package(GTest REQUIRED)
include(GoogleTest)

# Include directories
include_directories(../include)

# Test executable
set(TEST_SOURCES
    unit/test_order_pool.cpp
    unit/test_price_level.cpp
    unit/test_spsc_ring_buffer.cpp
    integration/test_matching_engine.cpp
    # Main test runner
    test_main.cpp
)

# Source files from main project
set(PROJECT_SOURCES
    ../src/types.cpp
    ../src/order_pool.cpp
    ../src/spsc_ring_buffer.cpp
    ../src/book.cpp
    ../src/matching_engine.cpp
    ../src/feed_handler.cpp
)

# Create test executable
add_executable(unit_tests ${TEST_SOURCES} ${PROJECT_SOURCES})

# Link libraries
target_link_libraries(unit_tests 
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)

# Set compiler flags for tests
target_compile_options(unit_tests PRIVATE
    -std=c++20
    -Wall
    -Wextra
    -O2
)

# Discover tests
gtest_discover_tests(unit_tests)

# Custom test targets
add_custom_target(run_tests
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/unit_tests
    DEPENDS unit_tests
    COMMENT "Running unit tests"
)

add_custom_target(run_tests_verbose
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/unit_tests --gtest_verbose
    DEPENDS unit_tests
    COMMENT "Running unit tests with verbose output"
)