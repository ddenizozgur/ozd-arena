#include "arena-c99/arena.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

char *cstr_fmtva(Arena *arena, const char *fmt, va_list args) {
  va_list copy_args;
  va_copy(copy_args, args);

  int bytes = vsnprintf(0, 0, fmt, args);
  if (bytes < 0) {
    assert(false && "vsnprintf(): failed");
    va_end(copy_args);
    return NULL;
  }

  Arena_Temp arena_state = arena_temp_begin(arena);

  size_t needed_bytes = bytes + 1ull;
  char *ptr = arena_push(arena, char, needed_bytes);
  if (ptr == NULL) {
    va_end(copy_args);
    return NULL;
  }

  int len = vsnprintf(ptr, needed_bytes, fmt, copy_args);
  va_end(copy_args);

  if (len < 0) {
    arena_temp_end(arena_state);

    assert(false && "vsnprintf(): failed");
    return NULL;
  }

  return ptr;
}

char *cstr_fmt(Arena *arena, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  char *res = cstr_fmtva(arena, fmt, args);
  va_end(args);
  return res;
}

void println_fmt(const char *fmt, ...) {
  Arena_Temp scratch = scratch_begin();

  va_list args;
  va_start(args, fmt);
  // Allocate the formatted string on temporary memory.
  const char *ptr = cstr_fmtva(scratch.arena, fmt, args);
  va_end(args);

  puts(ptr);

  scratch_end(scratch);
}

// This is not the best example but..
char *_remove_spaces0(Arena *arena, Arena *scratch, const char *input) {
  // Takes two arenas. While 'scratch' arena handles intermediate allocations,
  // 'arena' holds the final result.

  size_t max_len = strlen(input);
  char *tmp = arena_push(scratch, char, max_len + 1);

  size_t final_len = 0;
  for (size_t i = 0; i < max_len; ++i) {
    char c = input[i];
    if (c == ' ' || c == '\n' || c == '\t' || c == '\r' || c == '\f' ||
        c == '\v') {
    } else {
      tmp[final_len++] = c;
    }
  }
  tmp[final_len] = '\0';

  char *final = arena_push(arena, char, final_len + 1);
  memcpy(final, tmp, final_len + 1);

  return final;
}
char *remove_spaces(Arena *arena, const char *input) {
  // If the user passed the thread's scratch as the 'arena',
  // we bypass scratch_begin(). This prevents scratch_end() from
  // wiping the final result.
  if (arena_is_scratch(arena)) {
    return _remove_spaces0(arena, arena, input);
  }

  // Acquire temp memory, do work, release temp memory, return.
  Arena_Temp scratch = scratch_begin();
  char *result = _remove_spaces0(arena, scratch.arena, input);
  scratch_end(scratch);

  return result;
}

int main() {
  // Reserve a contiguous block of virtual address space (64 GB) upfront.
  // The OS will commit memory in chunks (8 KB) as we push.
  Arena arena = arena_init_ex(gigabytes(64), kilobytes(8));

  const char *cstr = cstr_fmt(&arena, "This is a test: \t%d", 46);
  puts(cstr);

  const char *space_free = remove_spaces(&arena, " Some Test\tV2\n! ");
  puts(space_free);

  arena_free(&arena);

  println_fmt("This is a test 2: \t%s", "testinator");

  scratch_free();
  return 0;
}
