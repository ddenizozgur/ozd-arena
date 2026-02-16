# ozd-arena
Simple, non-chained linear memory allocator (arena) implementation.

## Integration
This header is designed for single translation units.
You must include `ozd-arena.hpp` in exactly one implementation file (e.g., `main.cpp`).
Including it in multiple `.cpp` files will duplicate static state and break the implementation.

## Usage
```cpp
#include "ozd-arena.hpp"

#include <cstdio>
#include <cstdarg>

const char *cstr_fmtva(ozd::Arena *arena, const char *fmt, va_list args) {
    va_list copy_args;
    va_copy(copy_args, args);

    int bytes = vsnprintf(0, 0, fmt, args);
    if (bytes < 0) {    // bytes == 0: "" is valid string in c
        assert(false && "vsnprintf(): failed");
        va_end(copy_args);
        return {};
    }

    auto arena_state = ozd::temp_arena_begin(arena);

    size_t needed_bytes = bytes + 1ull;
    char *ptr = ozd::arena_push<char>(arena, needed_bytes);
    if (ptr == nullptr) {
        va_end(copy_args);
        return {};
    }

    int len = vsnprintf(ptr, needed_bytes, fmt, copy_args);
    va_end(copy_args);

    if (len < 0) {
        ozd::temp_arena_end(arena_state);

        assert(false && "vsnprintf(): failed");
        return {};
    }

    return ptr;
}

const char *cstr_fmt(ozd::Arena *arena, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const char *res = cstr_fmtva(arena, fmt, args);
    va_end(args);
    return res;
}

void tprint_fmt(const char *fmt, ...) {
    ozd::Temp_Arena scratch = ozd::scratch_begin();

    va_list args;
    va_start(args, fmt);
    const char *ptr = cstr_fmtva(scratch.arena, fmt, args);
    va_end(args);

    puts(ptr);

    ozd::scratch_end(scratch);
}

int main() {
    ozd::Arena arena = ozd::arena_init_ex(ozd::GiB(1), ozd::KiB(16));

    const char *cstr = cstr_fmt(&arena, "This is a test: %d\n", 46);
    puts(cstr);

    ozd::arena_free(&arena);

    tprint_fmt("This is a test 2: %s\n", "testinator");

    return 0;
}
