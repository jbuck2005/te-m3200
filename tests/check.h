// Tiny test helper: CHECK(cond) records a failure with its line, tests_done() prints the summary and sets the exit code.
#pragma once
#include <math.h>
#include <stdio.h>

static int g_checks = 0, g_failures = 0;
#define CHECK(cond)                                                    \
  do {                                                                 \
    g_checks++;                                                        \
    if (!(cond)) {                                                     \
      g_failures++;                                                    \
      printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);         \
    }                                                                  \
  } while (0)
#define CHECK_NEAR(a, b, tol) CHECK(fabsf((float) (a) - (float) (b)) <= (tol))

static int tests_done(const char *name) {
  printf("%s: %d checks, %d failed\n", name, g_checks, g_failures);
  return g_failures ? 1 : 0;
}
