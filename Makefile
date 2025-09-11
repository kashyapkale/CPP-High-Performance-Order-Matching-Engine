# Simple Makefile for direct compilation without CMake
CXX = g++
CXXFLAGS = -std=c++20 -O3 -march=native -pthread -Wall -Wextra
INCLUDES = -Iinclude
TARGET = order_matching_engine
SRCDIR = src
SOURCES = $(wildcard $(SRCDIR)/*.cpp)
OBJECTS = $(SOURCES:.cpp=.o)

.PHONY: all clean debug release

# Default target
all: release

# Release build (optimized)
release: CXXFLAGS += -DNDEBUG
release: $(TARGET)

# Debug build  
debug: CXXFLAGS = -std=c++20 -g -O0 -pthread -Wall -Wextra -DDEBUG
debug: $(TARGET)

# Link target
$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $@ $(CXXFLAGS)

# Compile source files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Clean build artifacts
clean:
	rm -f $(OBJECTS) $(TARGET)

# Run the program
run: $(TARGET)
	./$(TARGET)

# Print build info
info:
	@echo "Compiler: $(CXX)"
	@echo "Flags: $(CXXFLAGS)"
	@echo "Sources: $(SOURCES)"
	@echo "Target: $(TARGET)"