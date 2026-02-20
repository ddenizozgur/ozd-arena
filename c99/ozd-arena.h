#if !defined(_DEFS_ARENA_H)
#define _DEFS_ARENA_H

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
#else
#error arch not supported!
#endif

/*
 *
 */

#if _DEFS_COMPILER_MSVC
#define _THREAD_LOCAL   __declspec(thread)
#elif _DEFS_COMPILER_GCC || _DEFS_COMPILER_CLANG
#define _THREAD_LOCAL   __thread
#endif

#if _DEFS_COMPILER_MSVC
#define _AlignOf(T) __alignof(T)
#elif _DEFS_COMPILER_CLANG
#define _AlignOf(T) __alignof(T)
#elif _DEFS_COMPILER_GCC
#define _AlignOf(T) __alignof__(T)
#endif

/*
 *
 */

#define _IsPow2(x)               ((x) != 0 && ((x) & ((x) - 1)) == 0)
#define _IsPow2OrZero(x)         ((((x) - 1) & (x)) == 0)
#define _AlignUpPow2(n, align)   (((n) + ((align) - 1)) & ~((align) - 1))

#define Ozd_KiB(x)  ((x) << 10ull)
#define Ozd_MiB(x)  ((x) << 20ull)
#define Ozd_GiB(x)  ((x) << 30ull)

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

/*
 *
 */

#if _DEFS_OS_WINDOWS

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

static inline SYSTEM_INFO *_win32_get_sysinfo() {
    static SYSTEM_INFO sysInfo = { 0 };
#if _DEFS_ARCH_X64
    // we want our virtual address space big.
    GetSystemInfo(&sysInfo);
#endif
    // GetNativeSystemInfo(&sysInfo);
    return &sysInfo;
}

#elif DEFS_OS_LINUX

#include <sys/mman.h>
#include <unistd.h>

#endif  // DEFS_OS_

static inline void _os_abort(int code) {
#if _DEFS_OS_WINDOWS
    ExitProcess(code);
#elif _DEFS_OS_LINUX
    exit(code);
#endif
}

static inline size_t _os_get_pagesize() {
    static size_t res = 0;
#if _DEFS_OS_WINDOWS
    res = _win32_get_sysinfo()->dwPageSize;
#elif _DEFS_OS_LINUX
    res = sysconf(_SC_PAGESIZE);
#endif
    return res;
}

static inline void *_os_virtual_reserve(size_t size) {
    void *ptr = NULL;
#if _DEFS_OS_WINDOWS
    ptr = VirtualAlloc(NULL, size, MEM_RESERVE, PAGE_NOACCESS);
    assert(ptr != NULL && "VirtualAlloc(): reserve failed");
#elif _DEFS_OS_LINUX
    ptr = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) { // FATAL error
        assert(false && "mmap(): failed");
        _os_abort(1);
    }
#endif
    return ptr;
}

static inline void _os_virtual_commit(void *ptr, size_t size) {
#if _DEFS_OS_WINDOWS
    void *temp = VirtualAlloc(ptr, size, MEM_COMMIT, PAGE_READWRITE);
    if (temp == NULL) { // FATAL error
        assert(false && "VirtualAlloc(): FATAL");
        _os_abort(1);
    }
#elif _DEFS_OS_LINUX
    // user must manually align to page boundary for linux
    // check how-to for arena.h
    int res = mprotect(ptr, size, PROT_READ | PROT_WRITE);
    if (res == -1) {    // FATAL error
        assert(false && "mprotect(): commit failed");
        _os_abort(1);
    }
#endif
}

static inline void _os_virtual_decommit(void *ptr, size_t size) {
    assert(false && "TODO: check impl");
    return;

#if _DEFS_OS_WINDOWS
    if (!VirtualFree(ptr, 0, MEM_DECOMMIT))
        assert(false && "VirtualFree(): decommit failed");
#elif _DEFS_OS_LINUX
    mprotect(ptr, size, PROT_NONE); // order matters
    madvise(ptr, size, MADV_DONTNEED);
#endif
}

static inline void _os_virtual_release(void *ptr, size_t size) {
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

#define _DEFS_ARENA_DEFAULT_RESERVE_SIZE    (Ozd_MiB(128))

typedef struct ozd_Arena {
    void *ptr;
    size_t pos;
    size_t committed;
    size_t reserved;
    size_t perCommitSize;
} ozd_Arena;

static ozd_Arena ozd_arena_init_ex(size_t reserveSize, size_t perCommitSize) {
    size_t pageSize = _os_get_pagesize();

#if _DEFS_OS_WINDOWS
    // reserving less than 64KiB on windows is waste,
    // ptr must be align with dwAllocationGranularity
    reserveSize = _AlignUpPow2(reserveSize, _win32_get_sysinfo()->dwAllocationGranularity);
#elif _DEFS_OS_LINUX
    // linux can reserve 4KiB smallest, basically pagesize
    reserveSize = _AlignUpPow2(reserveSize, pagesize);
#endif

    // align per_commit_size with pagesize
    perCommitSize = _AlignUpPow2(perCommitSize, pageSize);
    // ptr is already aligned for us
    void *ptr = _os_virtual_reserve(reserveSize);
    if (ptr == NULL) return (ozd_Arena) { 0 };

    return (ozd_Arena) {
        .ptr = ptr,
        .reserved = reserveSize,
        .perCommitSize = perCommitSize,
    };
}

static inline ozd_Arena ozd_arena_init() {
    size_t perCommitSize = _os_get_pagesize() * 2;    // TODO:???
    return ozd_arena_init_ex(_DEFS_ARENA_DEFAULT_RESERVE_SIZE, perCommitSize);
}

static inline size_t ozd_arena_get_pos(const ozd_Arena *arena) {
    // unaligned !!!
    return arena->pos;
}

static void *ozd_arena_push_ex(ozd_Arena *arena, size_t size, size_t align) {
    // windows always zeroes fresh commits
    assert(_IsPow2(align) && "alignment must be non-zero power of 2");

    size_t lastPos = _AlignUpPow2(arena->pos, align);
    size_t postPos = lastPos + size;

    size_t reserved = arena->reserved;
    if (postPos > reserved) {
        assert(false && "reserved size exceeded");
        return NULL;
    }

    size_t committed = arena->committed;
    if (postPos > committed) {
        size_t needed = postPos - committed;
        size_t newCommit = _AlignUpPow2(needed, arena->perCommitSize);

        size_t maxCommit = reserved - committed;
        newCommit = newCommit < maxCommit ? newCommit : maxCommit;

        void *ptr = (char *)arena->ptr + committed;
        _os_virtual_commit(ptr, newCommit);

        arena->committed += newCommit;
    }

    void *res = (char *)arena->ptr + lastPos;
    arena->pos = postPos;
    return res;
}

static inline void ozd_arena_pop_to(ozd_Arena *arena, size_t to) { // TODO: decommit version
    assert(arena->pos >= to && "trying to pop forward");
    arena->pos = to;
}
static inline void ozd_arena_pop_by(ozd_Arena *arena, size_t by) { // TODO: decommit version
    ozd_arena_pop_to(arena, arena->pos - by);
}

static inline void ozd_arena_free(ozd_Arena *arena) {
    if (arena->ptr != NULL)
        _os_virtual_release(arena->ptr, arena->reserved);
    *arena = (ozd_Arena) { 0 };
}

#define Ozd_ArenaPush(arena, T, count)  (T *)ozd_arena_push_ex(arena, sizeof(T) * count, _AlignOf(T))
#define Ozd_ArenaPop(arena, T, count)   ozd_arena_pop_by(arena, sizeof(T) * count);

/*
 *
 */

typedef struct ozd_Arena_Temp {
    ozd_Arena *arena;
    size_t pos;
} ozd_Arena_Temp;

static inline ozd_Arena_Temp ozd_arena_temp_begin(ozd_Arena *arena)     { return (ozd_Arena_Temp) { arena, ozd_arena_get_pos(arena) }; }
static inline void ozd_arena_temp_end(ozd_Arena_Temp temp)              { ozd_arena_pop_to(temp.arena, temp.pos); }

/*
 *
 */

#define _DEFS_PER_THREAD_SCRATCH_COUNT  (2)
static _THREAD_LOCAL ozd_Arena _scratches[_DEFS_PER_THREAD_SCRATCH_COUNT] = { 0 };

static inline ozd_Arena *_scratches_get() {
    if (_scratches[0].ptr == NULL) {
        for (size_t i = 0; i < _DEFS_PER_THREAD_SCRATCH_COUNT; i++)
            _scratches[i] = ozd_arena_init();
    }
    return _scratches;
}

// requires null terminated conflict array
static inline bool _arena_has_conflict(const ozd_Arena *arena, const ozd_Arena *conflicts[]) {
    if (conflicts == NULL) return false;

    for (const ozd_Arena **it = conflicts; *it != NULL; it++)
        if (arena == *it) return true;

    return false;
}

static inline ozd_Arena *_arena_find_from_scratches(const ozd_Arena *conflicts[]) {
    ozd_Arena *scratches = _scratches_get();

    for (size_t i = 0; i < _DEFS_PER_THREAD_SCRATCH_COUNT; i++) {
        ozd_Arena *it = &scratches[i];
        if (!_arena_has_conflict(it, conflicts)) {
            return it;
        }
    }

    return NULL;
}

/*
 *
 */

static inline ozd_Arena_Temp _scratch_begin(const ozd_Arena *conflicts[]) {
    ozd_Arena *scratch = _arena_find_from_scratches(conflicts);
    if (scratch == NULL) {
        assert(false && "conflict with all scratch arenas");
        return (ozd_Arena_Temp) { 0 };
    }
    return ozd_arena_temp_begin(scratch);
}
// null terminated conflict array
#define Ozd_ScratchBegin(...)   _scratch_begin((const ozd_Arena *[]) { __VA_ARGS__, NULL })
#define Ozd_ScratchEnd(scratch) ozd_arena_temp_end(scratch)

static inline void ozd_scratches_free() {
    if (_scratches[0].ptr != NULL) {
        for (size_t i = 0; i < _DEFS_PER_THREAD_SCRATCH_COUNT; i++)
            ozd_arena_free(&_scratches[i]);
    }
}

/*
 *
 */

// TODO: bitmap allocator
// https://medium.com/@tom_84912/object-allocators-%E1%B4%99-us-dc0edda80c58

#endif  // _DEFS_ARENA_H
