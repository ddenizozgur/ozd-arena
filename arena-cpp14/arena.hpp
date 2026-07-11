#pragma once

#include "virtual.hpp"

constexpr size_t ARENA_DEFAULT_RESERVE_SIZE = megabytes(256);
constexpr size_t ARENA_DEFAULT_PER_COMMIT_SIZE = kilobytes(8);

struct Arena {
  void *ptr;
  size_t pos;
  size_t committed;
  size_t reserved;
  size_t per_commit_size;
};

inline Arena
arena_init(size_t reserve_size = ARENA_DEFAULT_RESERVE_SIZE,
           size_t per_commit_size = ARENA_DEFAULT_PER_COMMIT_SIZE) {
#if IS_OS_WINDOWS
  // reserving less than 64KiB on windows is waste,
  // ptr must be align with dwAllocationGranularity
  reserve_size = align_forward_pow2(reserve_size,
                                    os_win32_sysinfo.dwAllocationGranularity);
#elif IS_OS_LINUX
  reserve_size = align_forward_pow2(reserve_size, os_pagesize);
#endif

  per_commit_size = minimum(per_commit_size, reserve_size);

  // align per_commit_size with pagesize
  per_commit_size = align_forward_pow2(per_commit_size, os_pagesize);
  // ptr is already aligned for us
  void *ptr = os_virtual_reserve(reserve_size);
  if (ptr == nullptr)
    return {};

  Arena res = {};
  res.ptr = ptr;
  res.reserved = reserve_size;
  res.per_commit_size = per_commit_size;
  return res;
}

inline void *arena_push_ex(Arena *arena, size_t size, size_t alignment) {
  // Windows and Linux always zeroes fresh commits
  size_t last_pos = align_forward_pow2(arena->pos, alignment);
  size_t post_pos = last_pos + size;

  size_t reserved = arena->reserved;
  if (post_pos > reserved) {
    assert(false && "reserved size exceeded");
    return nullptr;
  }

  size_t committed = arena->committed;
  if (post_pos > committed) {
    size_t needed = post_pos - committed;
    size_t new_commit = align_forward(needed, arena->per_commit_size);

    size_t max_commit = reserved - committed;
    new_commit = minimum(new_commit, max_commit);

    void *ptr = static_cast<char *>(arena->ptr) + committed;
    if (!os_virtual_commit(ptr, new_commit))
      return nullptr;

    arena->committed += new_commit;
  }

  void *res = static_cast<char *>(arena->ptr) + last_pos;
  arena->pos = post_pos;
  return res;
}

inline void arena_pop_to(Arena *arena, size_t to) { // TODO: decommit version
  assert(arena->pos >= to && "trying to pop forward");
  arena->pos = to;
}
// inline void arena_pop_by(Arena *arena, size_t by) {
//   arena_pop_to(arena, arena->pos - by);
// }

inline void arena_free(Arena *arena) {
  if (arena->ptr != nullptr) {
    os_virtual_release(arena->ptr, arena->reserved);
  }
  *arena = {};
}

template <typename T> inline T *arena_push(Arena *arena, size_t count = 1) {
  void *ptr = arena_push_ex(arena, sizeof(T) * count, alignof(T));
  return static_cast<T *>(ptr);
}
// template <typename T> inline void arena_pop(Arena *arena, size_t count = 1) {
//   arena_pop_by(arena, sizeof(T) * count);
// }

struct Arena_Temp {
  Arena *arena;
  size_t pos;
};

inline Arena_Temp arena_temp_begin(Arena *arena) { return {arena, arena->pos}; }
inline void arena_temp_end(Arena_Temp temp) {
  arena_pop_to(temp.arena, temp.pos);
}

//
// Scratch
//
static thread_local Arena _scratch = {};

inline bool arena_is_scratch(const Arena *arena) {
  return (arena == &_scratch);
}

inline Arena *_scratch_get() {
  if (_scratch.ptr == nullptr) {
    _scratch = arena_init();
  }
  return &_scratch;
}

inline Arena_Temp scratch_begin() {
  auto scratch = _scratch_get();
  return arena_temp_begin(scratch);
}

inline bool scratch_end(Arena_Temp scratch) {
  if (!arena_is_scratch(scratch.arena)) {
    assert(false && "non-scratch argument passed");
    return false;
  }

  arena_temp_end(scratch);
  return true;
}

inline void scratch_free() {
  if (_scratch.ptr != nullptr) {
    arena_free(&_scratch);
  }
}

// TODO: bitmap allocator
// https://medium.com/@tom_84912/object-allocators-%E1%B4%99-us-dc0edda80c58
