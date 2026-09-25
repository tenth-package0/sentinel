// libFuzzer target: arbitrary bytes through ITCH framing and decoding must
// never crash or read out of bounds. Run with `make fuzz` (needs LLVM clang).

#include <cstddef>
#include <cstdint>

#include "sentinel/engine.hpp"
#include "sentinel/itch.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  sentinel::itch::Decoder decoder;
  sentinel::Config config;
  config.max_accounts = 4;
  config.max_symbols = 1 << 16;  // Every possible stock locate code.
  sentinel::Engine engine(config);
  sentinel::itch::for_each_message(data, size, [&](const std::uint8_t* message, std::size_t length) {
    if (const auto trade = decoder.decode(message, length)) engine.process(*trade);
  });
  return 0;
}
