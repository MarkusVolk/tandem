#pragma once

#include <cstdlib>
#include <print>

// Plain checks without a framework: a failed one names its place and ends the test.
#define CHECK(condition)                                                                                               \
  do {                                                                                                                 \
    if (!(condition)) {                                                                                                \
      std::println(stderr, "{}:{}: check failed: {}", __FILE__, __LINE__, #condition);                                 \
      std::exit(1);                                                                                                    \
    }                                                                                                                  \
  } while (false)
