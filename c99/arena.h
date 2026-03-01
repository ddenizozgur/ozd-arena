#if !defined(_ARENA_H)
#define _ARENA_H

#if defined(__clang__)
#define _IS_COMPILER_CLANG  1
#elif defined(_MSC_VER)
#define _IS_COMPILER_MSVC   1
#elif defined(__GNUC__)
#define _IS_COMPILER_GCC    1
#else
#error compiler not supported!
#endif

#if defined(_WIN32)
#define _IS_OS_WINDOWS      1
#elif defined(__linux__) // __gnu_linux__ just defined in kernel or smth...
#define _IS_OS_LINUX        1
#else
#error os not supported!
#endif

#if defined(_M_X64) || defined(__x86_64__) || defined(__amd64__)
#define _IS_ARCH_X64        1
#elif defined(__aarch64__)
    #if _IS_OS_LINUX
    #define _IS_ARCH_ARM64  1
    #endif
#error arch not supported!
#endif

/*
 *
 */

#if _IS_COMPILER_MSVC
#define _THREAD_LOCAL   __declspec(thread)
#elif _IS_COMPILER_GCC || _IS_COMPILER_CLANG
#define _THREAD_LOCAL   __thread
#endif

#if _IS_COMPILER_MSVC
#define _AlignOf(T) __alignof(T)
#elif _IS_COMPILER_CLANG
#define _AlignOf(T) __alignof(T)
#elif _IS_COMPILER_GCC
#define _AlignOf(T) __alignof__(T)
#endif

#define _GlueStep0(A, B)    A##B
#define Glue(A, B)          _GlueStep0(A, B)

#if _IS_COMPILER_MSVC
#pragma section(".CRT$XCU", read)
#define _Init(name) \
static void name(void); \
__declspec(allocate(".CRT$XCU")) \
static void (*Glue(name, _Ptr))(void) = name; \
static void name(void)
#elif _IS_COMPILER_GCC || _IS_COMPILER_CLANG
#define _Init(name) \
__attribute__((constructor)) \
static void name(void)
#endif  // IS_COMPILER_

/*
 *
 */

#include <stddef.h>
#include <stdint.h>
#include <assert.h>
#include <stdbool.h>

static inline bool _is_pow2(size_t x)                       { return (x != 0) && ((x & (x - 1)) == 0); }
static inline size_t _alignup_pow2(size_t n, size_t align)  { return (n + (align - 1)) & ~(align - 1); }

/*
 *
 */

static size_t _os_pageSize = 0;

#if _IS_OS_WINDOWS

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

static SYSTEM_INFO _win32_sysInfo = { 0 };
_Init(_win32_sysinfo_init) {
#if _IS_ARCH_X64
    GetSystemInfo(&_win32_sysInfo);
// #elif _IS_ARCH_X86
//     GetNativeSystemInfo(&_win32_sysInfo);
#endif
    _os_pageSize = _win32_sysInfo.dwPageSize;
}

#elif _IS_OS_LINUX

#include <sys/mman.h>
#include <unistd.h>

_Init(_linux_pagesize_init) {
    _os_pageSize = sysconf(_SC_PAGESIZE);
}

#endif  // _IS_OS_

static inline void *_os_virtual_reserve(size_t size) {
    void *ptr = NULL;
#if _IS_OS_WINDOWS
    ptr = VirtualAlloc(NULL, size, MEM_RESERVE, PAGE_NOACCESS);
    assert(ptr != NULL && "VirtualAlloc(): reserve failed");
#elif _IS_OS_LINUX
    ptr = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {    // FATAL error
        assert(false && "mmap(): reserve failed");
        return NULL;
    }
#endif
    return ptr;
}

static bool _os_virtual_commit(void *ptr, size_t size) {
#if _IS_OS_WINDOWS
    void *res = VirtualAlloc(ptr, size, MEM_COMMIT, PAGE_READWRITE);
    if (res == NULL) {
        DWORD lastErr = GetLastError();
        switch (lastErr) {
        case 0:
            assert(false && "VirtualAlloc(): commit failed. invalid argument");
            return false;
        default:
            assert(false && "VirtualAlloc(): commit failed");
            return false;
        }
    }
#elif _IS_OS_LINUX
    // manually align to page boundary for linux
    int res = mprotect(ptr, size, PROT_READ | PROT_WRITE);
    if (res == -1) {
        assert(false && "mprotect(): commit failed");
        return false;
    }
#endif
    return true;
}

static inline bool _os_virtual_release(void *ptr, size_t size) {
#if _IS_OS_WINDOWS
    (void)size;
    if (!VirtualFree(ptr, 0, MEM_RELEASE)) {
        assert(false && "VirtualFree(): release failed");
        return false;
    }
#elif _IS_OS_LINUX
    munmap(ptr, size);
#endif
    return true;
}

/*
 *
 */

#define KiB(n)  ((size_t)(n) << 10ull)
#define MiB(n)  ((size_t)(n) << 20ull)
#define GiB(n)  ((size_t)(n) << 30ull)

#define ARENA_DEFAULT_RESERVE_SIZE      (MiB(128))
#define ARENA_DEFAULT_PER_COMMIT_SIZE   (KiB(8))

typedef struct Arena {
    void *ptr;
    size_t pos;
    size_t committed;
    size_t reserved;
    size_t perCommitSize;
} Arena;

static Arena arena_init_ex(size_t reserveSize, size_t perCommitSize) {
#if _IS_OS_WINDOWS
    // reserving less than 64KiB on windows is waste,
    // ptr must be align with dwAllocationGranularity
    reserveSize = _alignup_pow2(reserveSize, _win32_sysInfo.dwAllocationGranularity);
#elif _IS_OS_LINUX
    // linux can reserve 4KiB smallest, basically pagesize
    reserveSize = _alignup_pow2(reserveSize, _os_pageSize);
#endif

    // align per_commit_size with pagesize
    perCommitSize = _alignup_pow2(perCommitSize, _os_pageSize);
    // ptr is already aligned for us
    void *ptr = _os_virtual_reserve(reserveSize);
    if (ptr == NULL) return (Arena) { 0 };

    return (Arena) {
        .ptr = ptr,
        .reserved = reserveSize,
        .perCommitSize = perCommitSize,
    };
}

static inline Arena arena_init() {
    return arena_init_ex(
        ARENA_DEFAULT_RESERVE_SIZE,
        ARENA_DEFAULT_PER_COMMIT_SIZE
    );
}

static inline size_t arena_get_pos(const Arena *arena) {
    // may unaligned !!!
    return arena->pos;
}

static void *arena_push_ex(Arena *arena, size_t size, size_t align) {
    // windows always zeroes fresh commits
    assert(_is_pow2(align) && "alignment must be non-zero power of 2");

    size_t lastPos = _alignup_pow2(arena->pos, align);
    size_t postPos = lastPos + size;

    size_t reserved = arena->reserved;
    if (postPos > reserved) {
        assert(false && "reserved size exceeded");
        return NULL;
    }

    size_t committed = arena->committed;
    if (postPos > committed) {
        size_t needed = postPos - committed;
        size_t newCommit = _alignup_pow2(needed, arena->perCommitSize);

        size_t maxCommit = reserved - committed;
        newCommit = newCommit < maxCommit ? newCommit : maxCommit;

        void *ptr = (char *)arena->ptr + committed;
        if (!_os_virtual_commit(ptr, newCommit)) return NULL;

        arena->committed += newCommit;
    }

    void *res = (char *)arena->ptr + lastPos;
    arena->pos = postPos;
    return res;
}

static inline void arena_pop_to(Arena *arena, size_t to) { // TODO: decommit version
    assert(arena->pos >= to && "trying to pop forward");
    arena->pos = to;
}
static inline void arena_pop_by(Arena *arena, size_t by) { // TODO: decommit version
    arena_pop_to(arena, arena->pos - by);
}

static inline void arena_free(Arena *arena) {
    if (arena->ptr != NULL)
        _os_virtual_release(arena->ptr, arena->reserved);
    *arena = (Arena) { 0 };
}

#define ArenaPush(arena, T, count)  (T *)arena_push_ex(arena, sizeof(T) * count, _AlignOf(T))
#define ArenaPop(arena, T, count)   arena_pop_by(arena, sizeof(T) * count)

/*
 *
 */

typedef struct Arena_Temp {
    Arena *arena;
    size_t pos;
} Arena_Temp;

static inline Arena_Temp arena_temp_begin(Arena *arena) { return (Arena_Temp) { arena, arena_get_pos(arena) }; }
static inline void arena_temp_end(Arena_Temp temp)      { arena_pop_to(temp.arena, temp.pos); }

/*
 *
 */

#define PER_THREAD_SCRATCH_COUNT    (4)

struct {
    Arena arr[PER_THREAD_SCRATCH_COUNT];
    bool taken[PER_THREAD_SCRATCH_COUNT];
} static _THREAD_LOCAL _scratchState = { 0 };

static inline Arena *_scratches_get() {
    if (_scratchState.arr[0].ptr == NULL) {
        for (size_t i = 0; i < PER_THREAD_SCRATCH_COUNT; i++)
            _scratchState.arr[i] = arena_init();
    }
    return _scratchState.arr;
}

/*
 *
 */

static inline Arena_Temp scratch_begin() {
    Arena *scratches = _scratches_get();

    for (size_t i = 0; i < PER_THREAD_SCRATCH_COUNT; i++) {
        if (!_scratchState.taken[i]) {
            _scratchState.taken[i] = true;
            return arena_temp_begin(&scratches[i]);
        }
    }

    assert(false && "conflict with all scratch arenas");
    return (Arena_Temp) { 0 };
}
static inline bool scratch_end(Arena_Temp scratch) {
    for (size_t i = 0; i < PER_THREAD_SCRATCH_COUNT; i++) {
        if (scratch.arena == &_scratchState.arr[i]) {
            _scratchState.taken[i] = false;
            arena_temp_end(scratch);
            return true;
        }
    }

    assert(false && "non-scratch arena passed to function");
    return false;
}

static inline void scratches_free() {
    if (_scratchState.arr[0].ptr != NULL) {
        for (size_t i = 0; i < PER_THREAD_SCRATCH_COUNT; i++) {
            arena_free(&_scratchState.arr[i]);
            _scratchState.taken[i] = false;
        }
    }
}

/*
 *
 */

// TODO: bitmap allocator
// https://medium.com/@tom_84912/object-allocators-%E1%B4%99-us-dc0edda80c58

#endif  // _ARENA_H
