CXX      ?= c++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror
CPPFLAGS += -Iinclude
BUILD    := build

CORE  := src/engine.cpp src/itch.cpp src/audit.cpp src/csv.cpp
TESTS := $(wildcard tests/*.cpp)
SAN   := -std=c++20 -O1 -g -fno-omit-frame-pointer

.PHONY: all test sanitize tsan benchmark fuzz wasm clean

all: $(BUILD)/demo $(BUILD)/tests $(BUILD)/benchmark $(BUILD)/itch_replay $(BUILD)/csv_ingest

$(BUILD):
	mkdir -p $@

$(BUILD)/demo: $(CORE) apps/demo.cpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

$(BUILD)/tests: $(CORE) $(TESTS) | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@ -pthread

$(BUILD)/benchmark: $(CORE) bench/benchmark.cpp bench/workload.hpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -O3 -DNDEBUG $(filter %.cpp,$^) -o $@

$(BUILD)/itch_replay: $(CORE) apps/itch_replay.cpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -O3 -DNDEBUG $^ -o $@ -pthread -lz

$(BUILD)/csv_ingest: $(CORE) apps/csv_ingest.cpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

test: $(BUILD)/tests
	./$(BUILD)/tests

# Address + undefined-behaviour sanitizers over the whole test suite.
sanitize: | $(BUILD)
	$(CXX) $(CPPFLAGS) $(SAN) -fsanitize=address,undefined -fno-sanitize-recover=all \
		$(CORE) $(TESTS) -o $(BUILD)/tests_asan -pthread
	./$(BUILD)/tests_asan

# Thread sanitizer: proves the lock-free ring has no data races.
tsan: | $(BUILD)
	$(CXX) $(CPPFLAGS) $(SAN) -fsanitize=thread $(CORE) $(TESTS) -o $(BUILD)/tests_tsan -pthread
	./$(BUILD)/tests_tsan

benchmark: $(BUILD)/benchmark
	./$(BUILD)/benchmark

# Needs an LLVM clang with libFuzzer (Apple clang does not ship it).
FUZZ_SECONDS ?= 60
fuzz: | $(BUILD)
	$(CXX) $(CPPFLAGS) $(SAN) -fsanitize=fuzzer,address,undefined $(CORE) fuzz/itch_fuzz.cpp \
		-o $(BUILD)/itch_fuzz
	./$(BUILD)/itch_fuzz -max_total_time=$(FUZZ_SECONDS)

wasm:
	mkdir -p web/public/wasm
	em++ -std=c++20 -O3 -Iinclude -fno-exceptions src/engine.cpp apps/wasm_bridge.cpp \
		-sMODULARIZE -sEXPORT_ES6 -sEXPORT_NAME=createSentinelModule -sENVIRONMENT=web \
		-sALLOW_MEMORY_GROWTH \
		-sEXPORTED_FUNCTIONS=_sentinel_configure,_sentinel_process,_sentinel_benchmark,_sentinel_positions,_sentinel_replay,_sentinel_reset \
		-sEXPORTED_RUNTIME_METHODS=ccall \
		-o web/public/wasm/sentinel.js

clean:
	rm -rf $(BUILD)
