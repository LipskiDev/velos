#pragma once

#include <cstdlib>
#include <iostream>

namespace Velos::Detail {
inline void HandleAssert(const char *expr, const char *file, int line) {
  std::cerr << "Assertion failed: " << expr << " in " << file << ":" << line
            << '\n';
  std::abort();
}
} // namespace Velos::Detail

#define VL_ASSERT(expr)                                                        \
  do {                                                                         \
    if (!(expr)) {                                                             \
      ::Velos::Detail::HandleAssert(#expr, __FILE__, __LINE__);                \
    }                                                                          \
  } while (0)
