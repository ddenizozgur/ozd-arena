#include "ozd-arena.h"

#include <stdio.h>
#include <stdarg.h>

const char *cstr_fmtva(ozd_Arena *arena, const char *fmt, va_list args) {
    va_list copyArgs;
    va_copy(copyArgs, args);

    int bytes = vsnprintf(0, 0, fmt, args);
    if (bytes < 0) {    // bytes == 0: "" is valid string in c
        assert(false && "vsnprintf(): failed");
        va_end(copyArgs);
        return NULL;
    }

    ozd_Arena_Temp arenaState = ozd_arena_temp_begin(arena);

    size_t neededBytes = bytes + 1ull;
    char *ptr = Ozd_ArenaPush(arena, char, neededBytes);
    if (ptr == NULL) {
        va_end(copyArgs);
        return NULL;
    }

    int len = vsnprintf(ptr, neededBytes, fmt, copyArgs);
    va_end(copyArgs);

    if (len < 0) {
        ozd_arena_temp_end(arenaState);

        assert(false && "vsnprintf(): failed");
        return NULL;
    }

    return ptr;
}

const char *cstr_fmt(ozd_Arena *arena, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const char *res = cstr_fmtva(arena, fmt, args);
    va_end(args);
    return res;
}

void tprintln_fmt(const char *fmt, ...) {
    ozd_Arena_Temp scratch = Ozd_ScratchBegin(NULL);

    va_list args;
    va_start(args, fmt);
    const char *ptr = cstr_fmtva(scratch.arena, fmt, args);
    va_end(args);

    puts(ptr);

    Ozd_ScratchEnd(scratch);
}

int main() {
    // ozd_Arena arena = ozd_arena_init_ex(Ozd_MiB(128), Ozd_KiB(8));
    ozd_Arena arena = ozd_arena_init();

    const char *cStr = cstr_fmt(&arena, "This is a test: \t%d", 46);
    puts(cStr);

    ozd_arena_free(&arena);

    tprintln_fmt("This is a test 2: \t%s", "testinator");

    ozd_scratches_free();
    return 0;
}
