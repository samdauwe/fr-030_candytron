/* The check macro shared by the suites: counts, and says what failed (the first 30 only). A suite returns g_failures != 0. */
#ifndef CANDYTRON_TEST_CHECK_H
#define CANDYTRON_TEST_CHECK_H

#include <stdio.h>

static int g_checks, g_failures;
#define CHECK(cond, ...)                            \
  do {                                              \
    g_checks++;                                     \
    if (!(cond)) {                                  \
      g_failures++;                                 \
      if (g_failures <= 30) {                       \
        printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__);                        \
        printf("\n");                               \
      }                                             \
    }                                               \
  } while (0)

#endif
