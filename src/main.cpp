#include "external/volk.h"
#include <stdio.h>
#include <assert.h>

#if BB_DEBUG
#define BB_ASSERT(exp) assert(exp)
#else
#define BB_ASSERT(exp)
#endif
#define BB_VK_ASSERT(exp)                                                      \
  do {                                                                         \
    auto result = exp;                                                         \
    BB_ASSERT(result == VK_SUCCESS);                                           \
  } while (0)

namespace bb {}

int main(int argc, char **argv) {
  BB_VK_ASSERT(volkInitialize());
  printf("Hello, World!");
  return 0;
}