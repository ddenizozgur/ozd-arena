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

  // Take a snapshot of the arena's current state.
  Arena_Temp arena_state = arena_temp_begin(arena);

  size_t needed_bytes = bytes + 1ull;
  char *ptr = arena_push(arena, char, needed_bytes);
  if (ptr == NULL) {
    va_end(copy_args);
    return NULL;
  }

  int len = vsnprintf(ptr, needed_bytes, fmt, copy_args);
  va_end(copy_args);

  // If the operation fails after we pushed memory, we restore the arena
  // to the saved state, reverting the allocation.
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
  // Grab the current thread's scratch arena.
  Arena_Temp scratch = scratch_begin();

  va_list args;
  va_start(args, fmt);
  // Allocate the formatted string on temporary memory.
  const char *ptr = cstr_fmtva(scratch.arena, fmt, args);
  va_end(args);

  puts(ptr);

  // Frees all memory pushed during this scratch block.
  scratch_end(scratch);
}

// Takes two arenas. The 'scratch' arena handles intermediate allocations
// whose sizes are unknown upfront. The 'arena' holds the final contiguous
// result.
char *_generate_slug0(Arena *arena, Arena *scratch, const char *input) {
  size_t max_len = strlen(input);

  // Push temporary working memory
  char *temp_buffer = arena_push(scratch, char, max_len + 1);

  size_t j = 0;
  bool last_was_hyphen = true;

  for (size_t i = 0; i < max_len; ++i) {
    char c = input[i];
    bool is_upper = (c >= 'A' && c <= 'Z');
    bool is_lower = (c >= 'a' && c <= 'z');
    bool is_num = (c >= '0' && c <= '9');

    if (is_upper || is_lower || is_num) {
      temp_buffer[j++] = is_upper ? (c + 32) : c;
      last_was_hyphen = false;
    } else if (!last_was_hyphen) {
      temp_buffer[j++] = '-';
      last_was_hyphen = true;
    }
  }

  if (j > 0 && temp_buffer[j - 1] == '-')
    j--;
  temp_buffer[j] = 0;

  // We now know the exact size. Push the final memory to the 'arena'.
  char *final_slug = arena_push(arena, char, j + 1);
  for (size_t i = 0; i <= j; ++i) {
    final_slug[i] = temp_buffer[i];
  }

  return final_slug;
}

char *generate_slug(Arena *arena, const char *input) {
  // If the user passed the thread's scratch as the 'arena',
  // we bypass scratch_begin(). This prevents scratch_end() from
  // wiping the final result.
  if (arena_is_scratch(arena)) {
    return _generate_slug0(arena, arena, input);
  }

  // Acquire temp memory, do work, release temp memory, return.
  Arena_Temp scratch = scratch_begin();
  char *ptr = _generate_slug0(arena, scratch.arena, input);
  scratch_end(scratch);

  return ptr;
}

int main() {
  // Reserve a contiguous block of virtual address space (64 GB) upfront.
  // The OS will commit memory in small chunks (8 KB) as we push.
  Arena arena = arena_init_ex(gigabytes(64), kilobytes(8));

  // This string lives as long as 'arena' lives.
  const char *cstr = cstr_fmt(&arena, "This is a test: \t%d", 46);
  puts(cstr);

  // Output: "some-library-v2"
  const char *slug = generate_slug(&arena, " Some Library: V2! ");
  puts(slug);

  arena_free(&arena);

  println_fmt("This is a test 2: \t%s", "testinator");

  // Release the thread-local scratch arena back to the OS before thread exit.
  scratch_free();
  return 0;
}
