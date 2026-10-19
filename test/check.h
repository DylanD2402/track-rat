/**
 * Track Rat - shared by the host-side tests
 *
 * CHECK(cond, printf-style message) counts a failure and says where.
 *
 * testRand() stands in for rand(), which gives different numbers with each C
 * library: a test fed random GPS noise passed on Windows and failed on Linux.
 * This is the Microsoft C runtime's generator, so every PC gets the numbers
 * the tests were written against.
 */

#ifndef TEST_CHECK_H
#define TEST_CHECK_H

#include <cstdint>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, ...)                          \
  do {                                            \
    if (!(cond)) {                                \
      failures++;                                 \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                        \
      printf("\n");                               \
    }                                             \
  } while (0)

static uint32_t testRandState = 1;
static inline void testSeed(uint32_t seed) { testRandState = seed; }
static inline int testRand() {  // 0..32767
  testRandState = testRandState * 214013u + 2531011u;
  return (int)((testRandState >> 16) & 0x7FFF);
}

#endif  // TEST_CHECK_H
