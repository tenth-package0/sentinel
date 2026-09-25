#include "test.hpp"

int main() {
  for (const test::Case& c : test::cases()) {
    const int before = test::failures;
    c.run();
    std::printf("%s %s\n", test::failures == before ? "PASS" : "FAIL", c.name);
  }
  std::printf("%zu tests, %d failed checks\n", test::cases().size(), test::failures);
  return test::failures == 0 ? 0 : 1;
}
