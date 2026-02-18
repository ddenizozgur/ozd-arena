#pragma once

/*
 * This header is designed for single translation units.
 */

#if defined(__clang__)
#define _DEFS_COMPILER_CLANG    1
#elif defined(_MSC_VER)
#define _DEFS_COMPILER_MSVC     1
#elif defined(__GNUC__)
#define _DEFS_COMPILER_GCC      1
#else
#error compiler not supported yet!
#endif

#if defined(_WIN32)
#define _DEFS_OS_WINDOWS        1
#elif defined(__linux__) // __gnu_linux__ just defined in kernel or smth...
#define _DEFS_OS_LINUX          1
#else
#error os not supported yet!
#endif

#if defined(_M_X64) || defined(__x86_64__) || defined(__amd64__)
#define _DEFS_ARCH_X64          1
#else
#error arch not supported yet!
#endif

/*
 *
 */

#if _DEFS_COMPILER_MSVC
#define _DEFS_FORCE_INLINE  static __forceinline
#define _DEFS_NO_INLINE     static __declspec(noinline)
#elif _DEFS_COMPILER_GCC || _DEFS_COMPILER_CLANG
#define _DEFS_FORCE_INLINE  static inline __attribute__((always_inline))
#define _DEFS_NO_INLINE     static __attribute__((noinline))
#endif

#define _GlueStep0(x, y)    x##y
#define _Glue(x, y)         _GlueStep0(x, y)

// thread safe
#define _DoOnceStep0(x)     _Glue(x, __COUNTER__)
#define _DoOnce(code)       static const char _DoOnceStep0(_do_once_) = [&](){ code; return 0; }()

#include <cassert>
#include <cstdint>
#include <cstdlib>

template <typename T> constexpr _DEFS_FORCE_INLINE T _Min(T x, T y) { return (x < y ? x : y); }
template <typename T> constexpr _DEFS_FORCE_INLINE T _Max(T x, T y) { return (x > y ? x : y); }

constexpr _DEFS_FORCE_INLINE bool _IsPow2(uint64_t x)       { return (x != 0 && (x & (x - 1)) == 0); }
constexpr _DEFS_FORCE_INLINE bool _IsPow2OrZero(uint64_t x) { return (((x - 1) & x) == 0); }
constexpr _DEFS_FORCE_INLINE uint64_t _AlignUpPow2(uint64_t n, uint64_t align) { return ((n + (align - 1)) & ~(align - 1)); }

namespace ozd {

constexpr _DEFS_FORCE_INLINE uint64_t KiB(uint64_t x) { return (x << 10ull); }
constexpr _DEFS_FORCE_INLINE uint64_t MiB(uint64_t x) { return (x << 20ull); }
constexpr _DEFS_FORCE_INLINE uint64_t GiB(uint64_t x) { return (x << 30ull); }

}

/*
 *
 */

#if _DEFS_OS_WINDOWS

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

inline const SYSTEM_INFO *_win32_get_sysinfo() {
    static SYSTEM_INFO sysinfo = {};
#if _DEFS_ARCH_X64
    // we want our virtual address space big
    _DoOnce( GetSystemInfo(&sysinfo); );
#endif
    // _DoOnce( GetNativeSystemInfo(&sysinfo); );
    return &sysinfo;
}

#elif _DEFS_OS_LINUX

#include <sys/mman.h>
#include <unistd.h>

#endif  // _DEFS_OS_

inline void _os_abort(int code) {
#if _DEFS_OS_WINDOWS
    ExitProcess(code);
#elif _DEFS_OS_LINUX
    exit(code);
#endif
}

static size_t _os_get_pagesize() {
    static size_t res = 0;
#if _DEFS_OS_WINDOWS
    res = _win32_get_sysinfo()->dwPageSize;
#elif _DEFS_OS_LINUX
    _DoOnce( res = sysconf(_SC_PAGESIZE); );
#endif
    return res;
}

inline void *_os_virtual_reserve(size_t size) {
    void *ptr = nullptr;
#if _DEFS_OS_WINDOWS
    ptr = VirtualAlloc(NULL, size, MEM_RESERVE, PAGE_NOACCESS);
    assert(ptr != nullptr && "VirtualAlloc(): reserve failed");
#elif _DEFS_OS_LINUX
    ptr = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {    // FATAL error
        assert(false && "mmap(): reserve failed");
        _os_abort(1);
    }
#endif
    return ptr;
};

inline void _os_virtual_commit(void *ptr, size_t size) {
#if _DEFS_OS_WINDOWS
    void *temp = VirtualAlloc(ptr, size, MEM_COMMIT, PAGE_READWRITE);
    if (temp == nullptr) { // FATAL error
        assert(false && "VirtualAlloc(): commit failed");
        _os_abort(1);
    }
#elif _DEFS_OS_LINUX
    int res = mprotect(ptr, size, PROT_READ | PROT_WRITE);
    if (res == -1) { // FATAL error
        assert(false && "mprotect(): commit failed");
        _os_abort(1);
    }
#endif
}

inline void _os_virtual_decommit(void *ptr, size_t size) {
#if _DEFS_OS_WINDOWS
    VirtualFree(ptr, size, MEM_DECOMMIT);
#elif _DEFS_OS_LINUX
    mprotect(ptr, size, PROT_NONE); // order matters
    madvise(ptr, size, MADV_DONTNEED);
#endif
}

inline void _os_virtual_release(void *ptr, size_t size) {
#if _DEFS_OS_WINDOWS
    (void)size;
    if (!VirtualFree(ptr, 0, MEM_RELEASE))
        assert(false && "VirtualFree(): release failed");
#elif _DEFS_OS_LINUX
    munmap(ptr, size);
#endif
}

/*
 *
 */

namespace ozd {

constexpr size_t ARENA_DEFAULT_RESERVE_SIZE = MiB(128);

struct Arena {
    void *ptr = nullptr;
    size_t pos = 0;
    size_t committed = 0;
    size_t reserved = 0;
    size_t per_commit_size = 0;
};

static Arena arena_init_ex(size_t reserve_size, size_t per_commit_size) {
    size_t pagesize = _os_get_pagesize();

#if _DEFS_OS_WINDOWS
    // reserving less than 64KiB on windows is waste,
    // ptr must be align with dwAllocationGranularity
    reserve_size = _AlignUpPow2(reserve_size, _win32_get_sysinfo()->dwAllocationGranularity);
#elif _DEFS_OS_LINUX
    // linux can reserve 4KiB smallest, basically pagesize
    reserve_size = _AlignUpPow2(reserve_size, pagesize);
#endif

    // align per_commit_size with pagesize
    per_commit_size = _AlignUpPow2(per_commit_size, pagesize);
    // ptr is already aligned for us
    void *ptr = _os_virtual_reserve(reserve_size);
    if (ptr == nullptr) return {};

    Arena res;
    res.ptr = ptr;
    res.reserved = reserve_size;
    res.per_commit_size = per_commit_size;
    return res;
}

inline Arena arena_init() {
    size_t per_commit_size = _os_get_pagesize() * 2;    // TODO:???
    return arena_init_ex(ARENA_DEFAULT_RESERVE_SIZE, per_commit_size);
}

_DEFS_FORCE_INLINE size_t arena_get_pos(const Arena *arena) {
    // unaligned !!!
    return arena->pos;
}

static void *arena_push_ex(Arena *arena, size_t size, size_t align) {
    // windows always zeroes fresh commits
    assert(_IsPow2(align) && "alignment must be non-zero power of 2");

    size_t last_pos = _AlignUpPow2(arena->pos, align);
    size_t post_pos = last_pos + size;

    size_t reserved = arena->reserved;
    if (post_pos > reserved) {
        assert(false && "reserved size exceeded");
        return nullptr;
    }

    size_t committed = arena->committed;
    if (post_pos > committed) {
        size_t needed = post_pos - committed;
        size_t new_commit = _AlignUpPow2(needed, arena->per_commit_size);

        size_t max_commit = reserved - committed;
        new_commit = _Min(new_commit, max_commit);

        void *ptr = (char *)arena->ptr + committed;
        _os_virtual_commit(ptr, new_commit);

        arena->committed += new_commit;
    }

    void *res = (char *)arena->ptr + last_pos;
    arena->pos = post_pos;
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
    if (arena->ptr != nullptr)
        _os_virtual_release(arena->ptr, arena->reserved);
    *arena = {};
}

template <typename T>
_DEFS_FORCE_INLINE T *arena_push(Arena *arena, size_t count = 1) {
    return (T *)arena_push_ex(arena, sizeof(T) * count, alignof(T));
}
template <typename T>
_DEFS_FORCE_INLINE void arena_pop(Arena *arena, size_t count = 1) {
    arena_pop_by(arena, sizeof(T) * count);
}

}

/*
 *
 */

namespace ozd {

struct Arena_Temp {
    Arena *arena = nullptr;
    size_t pos = 0;
};

_DEFS_FORCE_INLINE Arena_Temp arena_temp_begin(Arena *arena)  { return { arena, arena_get_pos(arena) }; }
_DEFS_FORCE_INLINE void arena_temp_end(Arena_Temp temp)       { arena_pop_to(temp.arena, temp.pos); }

constexpr size_t PER_THREAD_SCRATCH_COUNT = 2;

}

/*
 *
 */

static thread_local ozd::Arena
_scratches[ozd::PER_THREAD_SCRATCH_COUNT] = {};

inline ozd::Arena *_scratches_get() {
    if (_scratches[0].ptr == nullptr) {
        for (size_t i = 0; i < ozd::PER_THREAD_SCRATCH_COUNT; i++)
            _scratches[i] = ozd::arena_init();
    }
    return _scratches;
}

// null terminated conflict array
inline bool _arena_has_conflict(const ozd::Arena *arena, const ozd::Arena *conflicts[], size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (arena == conflicts[i]) return true;
    }
    return false;
}

inline ozd::Arena *_arena_find_from_scratches(const ozd::Arena *conflicts[], size_t count) {
    auto scratches = _scratches_get();

    for (size_t i = 0; i < ozd::PER_THREAD_SCRATCH_COUNT; i++) {
        auto it = &scratches[i];
        if (!_arena_has_conflict(it, conflicts, count)) {
            return it;
        }
    }

    return nullptr;
}

namespace ozd {

template <typename ...ArgV>
inline Arena_Temp scratch_begin(ArgV ...argv) {
    constexpr auto argc = sizeof...(ArgV);
    const Arena* conflicts[] = { argv... };

    auto scratch = _arena_find_from_scratches(conflicts, argc);
    if (scratch == nullptr) {
        assert(false && "conflict with all scratch arenas");
        return {};
    }

    return arena_temp_begin(scratch);
}
_DEFS_FORCE_INLINE void scratch_end(Arena_Temp scratch) {
    arena_temp_end(scratch);
}
inline void scratches_free() {
    if (_scratches[0].ptr != nullptr) {
        for (size_t i = 0; i < PER_THREAD_SCRATCH_COUNT; i++)
            arena_free(&_scratches[i]);
    }
}

}

/*
 *
 */

// TODO: bitmap allocator
// https://medium.com/@tom_84912/object-allocators-%E1%B4%99-us-dc0edda80c58
