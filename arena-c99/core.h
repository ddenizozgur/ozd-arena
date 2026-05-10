#pragma once

#include "pre.h"
#include <assert.h>
#include <stdbool.h>

#if IS_OS_LINUX
#include <stddef.h>
#endif

#if IS_COMPILER_MSVC
#define per_thread __declspec(thread)
#elif IS_COMPILER_CLANG || IS_COMPILER_GCC
#define per_thread __thread
#endif

#if IS_COMPILER_MSVC || IS_COMPILER_CLANG
#define align_of(T) __alignof(T)
#elif IS_COMPILER_GCC
#define align_of(T) __alignof__(T)
#endif

#if IS_COMPILER_MSVC
#pragma section(".CRT$XCU", read)
#define before_main(tag)                                                       \
  static void tag(void);                                                       \
  __declspec(allocate(".CRT$XCU")) static void (*glue(tag, _ptr))(void) = tag; \
  static void tag(void)
#elif IS_COMPILER_GCC || IS_COMPILER_CLANG
#define before_main(tag) __attribute__((constructor)) static void tag(void)
#endif

#define minimum(a, b) ((a) < (b) ? (a) : (b))
#define maximum(a, b) ((a) > (b) ? (a) : (b))

static inline bool is_pow2(size_t x) { return (x != 0) && !(x & (x - 1)); }
static inline bool is_pow2_or_zero(size_t x) { return !(x & (x - 1)); }

static inline size_t align_forward_pow2(size_t val, size_t alignment) {
  assert(is_pow2(alignment) && "alignment must be power of two");
  return (val + (alignment - 1)) & ~(alignment - 1);
}
static inline size_t align_forward(size_t val, size_t alignment) {
  size_t result = val + alignment - 1;
  return result - result % alignment;
}

static inline size_t kilobytes(size_t n) { return n * 1024ull; }
static inline size_t megabytes(size_t n) { return kilobytes(n) * 1024ull; }
static inline size_t gigabytes(size_t n) { return megabytes(n) * 1024ull; }
static inline size_t terabytes(size_t n) { return gigabytes(n) * 1024ull; }
