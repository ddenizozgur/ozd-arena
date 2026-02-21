#pragma once

#if defined(__clang__)
#define _DEFS_COMPILER_CLANG    1
#elif defined(_MSC_VER)
#define _DEFS_COMPILER_MSVC     1
#elif defined(__GNUC__)
#define _DEFS_COMPILER_GCC      1
#else
#error compiler not supported!
#endif

#if defined(_WIN32)
#define _DEFS_OS_WINDOWS        1
#elif defined(__linux__) // __gnu_linux__ just defined in kernel or smth...
#define _DEFS_OS_LINUX          1
#else
#error os not supported!
#endif

#if defined(_M_X64) || defined(__x86_64__) || defined(__amd64__)
#define _DEFS_ARCH_X64          1
#elif defined(__aarch64__)
    #if _DEFS_OS_LINUX
    #define _DEFS_ARCH_ARM64    1
    #endif
#error arch not supported!
#endif

/*
 *
 */

#define _GlueStep0(x, y)    x##y
#define _Glue(x, y)         _GlueStep0(x, y)

// thread safe
#define _DoOnceStep0(x)     _Glue(x, __COUNTER__)
#define _DoOnce(code)       static const char _DoOnceStep0(_do_once_) = [&](){ code; return 0; }()

#include <cassert>

static inline bool _is_pow2(size_t x)                       { return (x != 0) && ((x & (x - 1)) == 0); }
static inline size_t _alignup_pow2(size_t n, size_t align)  { return (n + (align - 1)) & ~(align - 1); }

/*
 *
 */

#if _DEFS_OS_WINDOWS

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

static inline SYSTEM_INFO *_win32_get_sysinfo() {
    static SYSTEM_INFO sysInfo = {};
    _DoOnce( GetSystemInfo(&sysInfo); );
    return &sysInfo;
}

#elif _DEFS_OS_LINUX

#include <sys/mman.h>
#include <unistd.h>

#endif  // _DEFS_OS_

static inline size_t _os_get_pagesize() {
    size_t res = 0;
#if _DEFS_OS_WINDOWS
    res = _win32_get_sysinfo()->dwPageSize;
#elif _DEFS_OS_LINUX
    _DoOnce( res = sysconf(_SC_PAGESIZE); );
#endif
    return res;
}

static inline void *_os_virtual_reserve(size_t size) {
    void *ptr = nullptr;
#if _DEFS_OS_WINDOWS
    ptr = VirtualAlloc(NULL, size, MEM_RESERVE, PAGE_NOACCESS);
    assert(ptr != NULL && "VirtualAlloc(): reserve failed");
#elif _DEFS_OS_LINUX
    ptr = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {    // FATAL error
        assert(false && "mmap(): reserve failed");
        return nullptr;
    }
#endif
    return ptr;
}

static bool _os_virtual_commit(void *ptr, size_t size) {
#if _DEFS_OS_WINDOWS
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
#elif _DEFS_OS_LINUX
    // manually align to page boundary for linux
    int res = mprotect(ptr, size, PROT_READ | PROT_WRITE);
    if (res == -1) {
        assert(false && "mprotect(): commit failed");
        return false;
    }
#endif
    return true;
}

static inline bool _os_virtual_decommit(void *ptr, size_t size) {
#if _DEFS_OS_WINDOWS
    if (!VirtualFree(ptr, size, MEM_DECOMMIT)) {
        assert(false && "VirtualFree(): decommit failed");
        return false;
    }
#elif _DEFS_OS_LINUX
    mprotect(ptr, size, PROT_NONE);
    madvise(ptr, size, MADV_DONTNEED);
#endif
    return true;
}

static inline bool _os_virtual_release(void *ptr, size_t size) {
#if _DEFS_OS_WINDOWS
    (void)size;
    if (!VirtualFree(ptr, 0, MEM_RELEASE)) {
        assert(false && "VirtualFree(): release failed");
        return false;
    }
#elif _DEFS_OS_LINUX
    munmap(ptr, size);
#endif
    return true;
}

/*
 *
 */

constexpr inline size_t KiB(size_t n)  { return n << 10ull; }
constexpr inline size_t MiB(size_t n)  { return n << 20ull; }
constexpr inline size_t GiB(size_t n)  { return n << 30ull; }

constexpr size_t ARENA_DEFAULT_RESERVE_SIZE = MiB(128);

struct Arena {
    void *ptr = nullptr;
    size_t pos = 0;
    size_t committed = 0;
    size_t reserved = 0;
    size_t perCommitSize = 0;
};

static Arena arena_init_ex(size_t reserveSize, size_t perCommitSize) {
    size_t pageSize = _os_get_pagesize();

#if _DEFS_OS_WINDOWS
    // reserving less than 64KiB on windows is waste,
    // ptr must be align with dwAllocationGranularity
    reserveSize = _alignup_pow2(reserveSize, _win32_get_sysinfo()->dwAllocationGranularity);
#elif _DEFS_OS_LINUX
    // linux can reserve 4KiB smallest, basically pagesize
    reserveSize = _alignup_pow2(reserveSize, pageSize);
#endif

    // align per_commit_size with pagesize
    perCommitSize = _alignup_pow2(perCommitSize, pageSize);
    // ptr is already aligned for us
    void *ptr = _os_virtual_reserve(reserveSize);
    if (ptr == NULL) return (Arena) { 0 };

    Arena res = {};
    res.ptr = ptr;
    res.reserved = reserveSize;
    res.perCommitSize = perCommitSize;
    return res;
}

static inline Arena arena_init() {
    size_t perCommitSize = _os_get_pagesize() * 2;  // TODO:???
    return arena_init_ex(ARENA_DEFAULT_RESERVE_SIZE, perCommitSize);
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
        return nullptr;
    }

    size_t committed = arena->committed;
    if (postPos > committed) {
        size_t needed = postPos - committed;
        size_t newCommit = _alignup_pow2(needed, arena->perCommitSize);

        size_t maxCommit = reserved - committed;
        newCommit = newCommit < maxCommit ? newCommit : maxCommit;

        void *ptr = (char *)arena->ptr + committed;
        if (!_os_virtual_commit(ptr, newCommit)) return nullptr;

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
    if (arena->ptr != nullptr)
        _os_virtual_release(arena->ptr, arena->reserved);
    *arena = {};
}

template <typename T>
static inline T *ArenaPush(Arena *arena, size_t count = 1) {
    return (T *)arena_push_ex(arena, sizeof(T) * count, alignof(T));
}
template <typename T>
static inline void ArenaPop(Arena *arena, size_t count = 1) {
    arena_pop_by(arena, sizeof(T) * count);
}

/*
 *
 */

struct Arena_Temp {
    Arena *arena = nullptr;
    size_t pos = 0;
};

static inline Arena_Temp arena_temp_begin(Arena *arena)  { return { arena, arena_get_pos(arena) }; }
static inline void arena_temp_end(Arena_Temp temp)       { arena_pop_to(temp.arena, temp.pos); }

/*
 *
 */

constexpr size_t PER_THREAD_SCRATCH_COUNT = 2;

static thread_local Arena _scratches[PER_THREAD_SCRATCH_COUNT] = {};

static inline Arena *_scratches_get() {
    if (_scratches[0].ptr == nullptr) {
        for (size_t i = 0; i < PER_THREAD_SCRATCH_COUNT; i++)
            _scratches[i] = arena_init();
    }
    return _scratches;
}

static inline bool _arena_has_conflict(const Arena *arena, const Arena *conflicts[], size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (arena == conflicts[i]) return true;
    }
    return false;
}

static inline Arena *_arena_find_from_scratches(const Arena *conflicts[], size_t count) {
    auto scratches = _scratches_get();

    for (size_t i = 0; i < PER_THREAD_SCRATCH_COUNT; i++) {
        auto it = &scratches[i];
        if (!_arena_has_conflict(it, conflicts, count)) {
            return it;
        }
    }

    return nullptr;
}

template <typename ...ArgV>
inline Arena_Temp ScratchBegin(ArgV ...argv) {
    constexpr auto argc = sizeof...(ArgV);
    const Arena *conflicts[] = { argv... };

    auto scratch = _arena_find_from_scratches(conflicts, argc);
    if (scratch == nullptr) {
        assert(false && "conflict with all scratch arenas");
        return {};
    }

    return arena_temp_begin(scratch);
}
static inline void ScratchEnd(Arena_Temp scratch) {
    arena_temp_end(scratch);
}
inline void scratches_free() {
    if (_scratches[0].ptr != nullptr) {
        for (size_t i = 0; i < PER_THREAD_SCRATCH_COUNT; i++)
            arena_free(&_scratches[i]);
    }
}

/*
 *
 */

// TODO: bitmap allocator
// https://medium.com/@tom_84912/object-allocators-%E1%B4%99-us-dc0edda80c58
