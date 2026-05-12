#include <stdlib.h>
#include <unistd.h>

/* POSIX requires support for at least ATEXIT_MAX (>=32) handlers. The
 * table is process-local — execve resets it because the new image
 * starts fresh, and fork inherits it (each child runs its own copy on
 * exit). */
#define ATEXIT_MAX 32
static void (*atexit_fns[ATEXIT_MAX])(void);
static int   atexit_count;

int atexit(void (*func)(void)) {
    if (!func) return -1;
    if (atexit_count >= ATEXIT_MAX) return -1;
    atexit_fns[atexit_count++] = func;
    return 0;
}

static __attribute__((noreturn)) void raw_sys_exit(int status) {
    register long _a7 asm("a7") = 1;   /* SYS_exit */
    register long _a0 asm("a0") = status;
    asm volatile("ecall" : : "r"(_a7), "r"(_a0) : "memory");
    __builtin_unreachable();
}

void exit(int status) {
    /* Handlers run LIFO. Decrement count *before* calling so that a
     * handler which itself calls exit() walks the older portion of the
     * stack only — otherwise the same handler would re-enter forever. */
    while (atexit_count > 0) {
        void (*fn)(void) = atexit_fns[--atexit_count];
        if (fn) fn();
    }
    raw_sys_exit(status);
}

/* POSIX: _exit and _Exit must skip atexit handlers (and any stdio
 * flushing, if we had any). */
void _exit(int status) {
    raw_sys_exit(status);
}
