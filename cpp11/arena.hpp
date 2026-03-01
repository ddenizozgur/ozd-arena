#pragma once

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

#include <stddef.h>
#include <assert.h>

#define _GlueStep0(x, y)    x##y
#define _Glue(x, y)         _GlueStep0(x, y)

// thread safe
#define _Init(name) \
static void name(); \
static int _Glue(name, Init) = []() { name(); return 0; }(); \
static void name()

constexpr size_t KiB(size_t n) { return n << 10ull; }
constexpr size_t MiB(size_t n) { return n << 20ull; }
constexpr size_t GiB(size_t n) { return n << 30ull; }

constexpr bool IsPow2(size_t x)         { return x != 0 && (x & (x - 1)) == 0; }
constexpr bool IsPow2OrZero(size_t x)   { return ((x - 1) & x) == 0; }
constexpr size_t AlignUpPow2(size_t n, size_t align) {
    return (n + (align - 1)) & ~(align - 1);
}

/*
 *
 */

static size_t _os_pageSize = 0;

#if _IS_OS_WINDOWS

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

static SYSTEM_INFO _win32_sysInfo = {};
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

inline void *_os_virtual_reserve(size_t size) {
    void *ptr = nullptr;
#if _IS_OS_WINDOWS
    ptr = VirtualAlloc(NULL, size, MEM_RESERVE, PAGE_NOACCESS);
    assert(ptr != NULL && "VirtualAlloc(): reserve failed");
#elif _IS_OS_LINUX
    ptr = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {    // FATAL error
        assert(false && "mmap(): reserve failed");
        return nullptr;
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

inline bool _os_virtual_decommit(void *ptr, size_t size) {
#if _IS_OS_WINDOWS
    if (!VirtualFree(ptr, size, MEM_DECOMMIT)) {
        assert(false && "VirtualFree(): decommit failed");
        return false;
    }
#elif _IS_OS_LINUX
    mprotect(ptr, size, PROT_NONE);
    madvise(ptr, size, MADV_DONTNEED);
#endif
    return true;
}

inline bool _os_virtual_release(void *ptr, size_t size) {
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

constexpr size_t ARENA_DEFAULT_RESERVE_SIZE = MiB(64);
constexpr size_t ARENA_DEFAULT_PER_COMMIT_SIZE = KiB(8);

struct Arena {
    void *ptr;
    size_t pos;
    size_t committed;
    size_t reserved;
    size_t perCommitSize;
};

static Arena arena_init(
    size_t reserveSize = ARENA_DEFAULT_RESERVE_SIZE,
    size_t perCommitSize = ARENA_DEFAULT_PER_COMMIT_SIZE
) {
#if _IS_OS_WINDOWS
    // reserving less than 64KiB on windows is waste,
    // ptr must be align with dwAllocationGranularity
    reserveSize = AlignUpPow2(reserveSize, _win32_sysInfo.dwAllocationGranularity);
#elif _IS_OS_LINUX
    // linux can reserve 4KiB smallest, basically pagesize
    reserveSize = AlignUpPow2(reserveSize, _os_pageSize);
#endif

    perCommitSize = perCommitSize < reserveSize ? perCommitSize : reserveSize;

    // align per_commit_size with pagesize
    perCommitSize = AlignUpPow2(perCommitSize, _os_pageSize);
    // ptr is already aligned for us
    void *ptr = _os_virtual_reserve(reserveSize);
    if (ptr == nullptr) return {};

    Arena res = {};
    res.ptr = ptr;
    res.reserved = reserveSize;
    res.perCommitSize = perCommitSize;
    return res;
}

inline size_t arena_get_pos(const Arena *arena) {
    // may unaligned !!!
    return arena->pos;
}

static void *arena_push_ex(Arena *arena, size_t size, size_t align) {
    // windows and linux always zeroes fresh commits
    assert(IsPow2(align) && "alignment must be non-zero power of 2");

    size_t lastPos = AlignUpPow2(arena->pos, align);
    size_t postPos = lastPos + size;

    size_t reserved = arena->reserved;
    if (postPos > reserved) {
        assert(false && "reserved size exceeded");
        return nullptr;
    }

    size_t committed = arena->committed;
    if (postPos > committed) {
        size_t needed = postPos - committed;
        size_t newCommit = AlignUpPow2(needed, arena->perCommitSize);

        size_t maxCommit = reserved - committed;
        newCommit = newCommit < maxCommit ? newCommit : maxCommit;

        void *ptr = static_cast<char *>(arena->ptr) + committed;
        if (!_os_virtual_commit(ptr, newCommit)) return nullptr;

        arena->committed += newCommit;
    }

    void *res = static_cast<char *>(arena->ptr) + lastPos;
    arena->pos = postPos;
    return res;
}

inline void arena_pop_to(Arena *arena, size_t to) { // TODO: decommit version
    assert(arena->pos >= to && "trying to pop forward");
    arena->pos = to;
}
inline void arena_pop_by(Arena *arena, size_t by) { // TODO: decommit version
    arena_pop_to(arena, arena->pos - by);
}

inline void arena_free(Arena *arena) {
    if (arena->ptr != nullptr) {
        _os_virtual_release(arena->ptr, arena->reserved);
    }
    *arena = {};
}

template <typename T>
inline T *arena_push(Arena *arena, size_t count = 1) {
    void *ptr = arena_push_ex(arena, sizeof(T) * count, alignof(T));
    return static_cast<T *>(ptr);
}
template <typename T>
inline void arena_pop(Arena *arena, size_t count = 1) {
    arena_pop_by(arena, sizeof(T) * count);
}

/*
 *
 */

struct Arena_Temp {
    Arena *arena;
    size_t pos;
};

inline Arena_Temp arena_temp_begin(Arena *arena) { return { arena, arena_get_pos(arena) }; }
inline void arena_temp_end(Arena_Temp temp)      { arena_pop_to(temp.arena, temp.pos); }

/*
 *
 */

constexpr unsigned int PER_THREAD_SCRATCH_COUNT = 4;

struct {
    Arena arr[PER_THREAD_SCRATCH_COUNT] = {};
    bool taken[PER_THREAD_SCRATCH_COUNT] = {};
} static thread_local _scratchState = {};

inline Arena *_scratches_get() {
    if (_scratchState.arr[0].ptr == nullptr) {
        for (size_t i = 0; i < PER_THREAD_SCRATCH_COUNT; i++)
            _scratchState.arr[i] = arena_init();
    }
    return _scratchState.arr;
}

inline Arena_Temp scratch_begin() {
    auto scratches = _scratches_get();

    for (size_t i = 0; i < PER_THREAD_SCRATCH_COUNT; i++) {
        if (!_scratchState.taken[i]) {
            _scratchState.taken[i] = true;
            return arena_temp_begin(&scratches[i]);
        }
    }

    assert(false && "conflict with all scratch arenas");
    return {};
}
inline void scratch_end(Arena_Temp scratch) {
    for (size_t i = 0; i < PER_THREAD_SCRATCH_COUNT; i++) {
        if (scratch.arena == &_scratchState.arr[i]) {
            _scratchState.taken[i] = false;
            arena_temp_end(scratch);
            return;
        }
    }
    assert(false && "non-scratch arena passed to function");
}

inline void scratches_free() {
    if (_scratchState.arr[0].ptr != nullptr) {
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
