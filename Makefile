CXX ?= c++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic -Iinclude
BUILD_DIR := build
ENGINE := src/engine.cpp

.PHONY: all test benchmark clean

all: $(BUILD_DIR)/sentinel_demo $(BUILD_DIR)/sentinel_tests $(BUILD_DIR)/sentinel_benchmark

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/sentinel_demo: $(ENGINE) apps/demo.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD_DIR)/sentinel_tests: $(ENGINE) tests/engine_tests.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD_DIR)/sentinel_benchmark: $(ENGINE) bench/benchmark.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

test: $(BUILD_DIR)/sentinel_tests
	./$(BUILD_DIR)/sentinel_tests

benchmark: $(BUILD_DIR)/sentinel_benchmark
	./$(BUILD_DIR)/sentinel_benchmark

clean:
	rm -rf $(BUILD_DIR)

