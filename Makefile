CXX ?= c++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic -Iinclude
BUILD_DIR := build
ENGINE := src/engine.cpp

.PHONY: all test benchmark wasm clean

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

wasm:
	mkdir -p web/public/wasm
	em++ -std=c++20 -O3 -Iinclude src/engine.cpp apps/wasm_bridge.cpp \
		-s WASM=1 -s MODULARIZE=1 -s EXPORT_ES6=1 \
		-s EXPORT_NAME=createSentinelModule -s ENVIRONMENT=web \
		-s ALLOW_MEMORY_GROWTH=1 \
		-s EXPORTED_FUNCTIONS='["_sentinel_configure","_sentinel_process","_sentinel_positions","_sentinel_replay","_sentinel_reset","_sentinel_processed_count"]' \
		-s EXPORTED_RUNTIME_METHODS='["ccall"]' \
		-o web/public/wasm/sentinel.js

clean:
	rm -rf $(BUILD_DIR)
