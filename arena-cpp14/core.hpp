#pragma once

#include "pre.hpp"
#include <assert.h>

#if IS_OS_LINUX
#include <stddef.h>
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

template <typename T> constexpr T minimum(T a, T b) { return a < b ? a : b; }
template <typename T> constexpr T maximum(T a, T b) { return a > b ? a : b; }

constexpr bool is_pow2(size_t x) { return (x != 0) && !(x & (x - 1)); }
constexpr bool is_pow2_or_zero(size_t x) { return !(x & (x - 1)); }

constexpr size_t align_forward_pow2(size_t val, size_t alignment) {
  assert(is_pow2(alignment) && "alignment must be power of two");
  return (val + (alignment - 1)) & ~(alignment - 1);
}
constexpr size_t align_forward(size_t val, size_t alignment) {
  auto result = val + alignment - 1;
  return result - result % alignment;
}

constexpr size_t kilobytes(size_t n) { return n * 1024; }
constexpr size_t megabytes(size_t n) { return kilobytes(n) * 1024; }
constexpr size_t gigabytes(size_t n) { return megabytes(n) * 1024; }
constexpr size_t terabytes(size_t n) { return gigabytes(n) * 1024; }

constexpr size_t thousand(size_t n) { return n * 1000; }
constexpr size_t million(size_t n) { return thousand(n) * 1000; }
constexpr size_t billion(size_t n) { return million(n) * 1000; }
