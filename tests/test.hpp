#pragma once

// A minimal test harness: TEST registers a function, CHECK records failures.

#include <cstdio>
#include <vector>

namespace test {

struct Case {
  const char* name;
  void (*run)();
};

inline std::vector<Case>& cases() {
  static std::vector<Case> all;
  return all;
}

inline int failures = 0;

struct Register {
  Register(const char* name, void (*run)()) { cases().push_back({name, run}); }
};

inline void check(bool ok, const char* expression, const char* file, int line) {
  if (!ok) {
    ++failures;
    std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", file, line, expression);
  }
}

}  // namespace test

#define TEST(name)                                        \
  static void name();                                     \
  static const test::Register name##_register(#name, name); \
  static void name()

#define CHECK(expression) test::check(static_cast<bool>(expression), #expression, __FILE__, __LINE__)
