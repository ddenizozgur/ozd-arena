#include "ozd-arena.hpp"

#include <cstdio>
#include <cstdarg>

const char *cstr_fmtva(ozd::Arena *arena, const char *fmt, va_list args) {
    va_list copyArgs;
    va_copy(copyArgs, args);

    int bytes = vsnprintf(0, 0, fmt, args);
    if (bytes < 0) {    // bytes == 0: "" is valid string in c
        assert(false && "vsnprintf(): failed");
        va_end(copyArgs);
        return nullptr;
    }

    auto arena_state = ozd::arena_temp_begin(arena);

    size_t neededBytes = bytes + 1ull;
    char *ptr = ozd::arena_push<char>(arena, neededBytes);
    if (ptr == nullptr) {
        va_end(copyArgs);
        return nullptr;
    }

    int len = vsnprintf(ptr, neededBytes, fmt, copyArgs);
    va_end(copyArgs);

    if (len < 0) {
        ozd::arena_temp_end(arena_state);

        assert(false && "vsnprintf(): failed");
        return nullptr;
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

void tprintln_fmt(const char *fmt, ...) {
    ozd::Arena_Temp scratch = ozd::scratch_begin();

    va_list args;
    va_start(args, fmt);
    const char *ptr = cstr_fmtva(scratch.arena, fmt, args);
    va_end(args);

    puts(ptr);

    ozd::scratch_end(scratch);
}

int main() {
    // ozd::Arena arena = ozd::arena_init_ex(ozd::MiB(128), ozd::KiB(8));
    ozd::Arena arena = ozd::arena_init();

    const char *cStr = cstr_fmt(&arena, "This is a test: \t%d", 46);
    puts(cStr);

    ozd::arena_free(&arena);

    tprintln_fmt("This is a test 2: \t%s", "testinator");

    ozd::scratches_free();
    return 0;
}
