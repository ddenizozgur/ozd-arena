#pragma once

/*
 *
 */

#if defined(__clang__)

#define IS_COMPILER_CLANG 1

#if defined(_WIN32)
#define IS_OS_WINDOWS 1
#elif defined(__gnu_linux__) || defined(__linux__)
#define IS_OS_LINUX 1
#else
#error This compiler/OS combo is not supported.
#endif

#if defined(__amd64__) || defined(__amd64) || defined(__x86_64__) ||           \
    defined(__x86_64)
#define IS_ARCH_X64 1
// # elif defined(i386) || defined(__i386) || defined(__i386__)
// #  define IS_ARCH_X86   1
#elif defined(__aarch64__)
#define IS_ARCH_ARM64 1
// # elif defined(__arm__)
// #  define IS_ARCH_ARM32 1
#else
#error Architecture not supported.
#endif

/*
 *
 */

#elif defined(_MSC_VER)

#define IS_COMPILER_MSVC 1

#if defined(_WIN32)
#define IS_OS_WINDOWS 1
#else
#error This compiler/OS combo is not supported.
#endif

#if defined(_M_AMD64)
#define IS_ARCH_X64 1
// # elif defined(_M_IX86)
// #  define IS_ARCH_X86 1
#elif defined(_M_ARM64)
#define IS_ARCH_ARM64 1
// # elif defined(_M_ARM)
// #  define IS_ARCH_ARM32 1
#else
#error Architecture not supported.
#endif

/*
 *
 */

#elif defined(__GNUC__) || defined(__GNUG__)

#define IS_COMPILER_GCC 1

#if defined(__gnu_linux__) || defined(__linux__)
#define IS_OS_LINUX 1
#else
#error This compiler/OS combo is not supported.
#endif

#if defined(__amd64__) || defined(__amd64) || defined(__x86_64__) ||           \
    defined(__x86_64)
#define IS_ARCH_X64 1
// # elif defined(i386) || defined(__i386) || defined(__i386__)
// #  define IS_ARCH_X86 1
#elif defined(__aarch64__)
#define IS_ARCH_ARM64 1
// # elif defined(__arm__)
// #  define IS_ARCH_ARM32 1
#else
#error Architecture not supported.
#endif

#else
#error Compiler not supported.
#endif

/*
 *
 */

#if !defined(IS_ARCH_X64)
#define IS_ARCH_X64 0
#endif
// #if !defined(IS_ARCH_X86)
// # define IS_ARCH_X86    0
// #endif
#if !defined(IS_ARCH_ARM64)
#define IS_ARCH_ARM64 0
#endif
// #if !defined(IS_ARCH_ARM32)
// # define IS_ARCH_ARM32  0
// #endif
#if !defined(IS_COMPILER_MSVC)
#define IS_COMPILER_MSVC 0
#endif
#if !defined(IS_COMPILER_GCC)
#define IS_COMPILER_GCC 0
#endif
#if !defined(IS_COMPILER_CLANG)
#define IS_COMPILER_CLANG 0
#endif
#if !defined(IS_OS_WINDOWS)
#define IS_OS_WINDOWS 0
#endif
#if !defined(IS_OS_LINUX)
#define IS_OS_LINUX 0
#endif

/*
 *
 */
