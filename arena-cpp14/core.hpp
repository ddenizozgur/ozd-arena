#pragma once

#include "pre.hpp"
#include <assert.h>

#define glue_0(A, B) A##B
#define glue(A, B) glue_0(A, B)

#if IS_COMPILER_MSVC
#pragma section(".CRT$XCU", read)
#define before_main(tag)                                                       \
  static void tag(void);                                                       \
  __declspec(allocate(".CRT$XCU")) static void (*glue(tag, _ptr))(void) = tag; \
  static void tag(void)
#elif IS_COMPILER_GCC || IS_COMPILER_CLANG
#define before_main(tag) __attribute__((constructor)) static void tag(void)
#endif

using usize = decltype(sizeof(0));

template <typename T> constexpr T minimum(T a, T b) { return a < b ? a : b; }
template <typename T> constexpr T maximum(T a, T b) { return a > b ? a : b; }

constexpr bool is_pow2(usize x) { return (x != 0) && !(x & (x - 1)); }
constexpr bool is_pow2_or_zero(usize x) { return !(x & (x - 1)); }

constexpr usize align_forward_pow2(usize val, usize alignment) {
  assert(is_pow2(alignment) && "alignment must be power of two");
  return (val + (alignment - 1)) & ~(alignment - 1);
}
constexpr usize align_forward(usize val, usize alignment) {
  auto result = val + alignment - 1;
  return result - result % alignment;
}

constexpr usize kilobytes(usize n) { return n * 1024; }
constexpr usize megabytes(usize n) { return kilobytes(n) * 1024; }
constexpr usize gigabytes(usize n) { return megabytes(n) * 1024; }
constexpr usize terabytes(usize n) { return gigabytes(n) * 1024; }

constexpr usize thousand(usize n) { return n * 1000; }
constexpr usize million(usize n) { return thousand(n) * 1000; }
constexpr usize billion(usize n) { return million(n) * 1000; }
