/*
 * stack_overflow_test — verify that unbounded recursion is killed with
 * SIGSEGV once the stack VMA hits its MAX_STACK cap, not silently
 * corrupting the heap or other VMAs below.
 *
 * Parent forks a child that recurses indefinitely, each frame burning a
 * 4 KiB on-stack buffer. The child must be killed; parent must survive
 * and observe a non-zero exit status matching SIGSEGV.
 *
 * A separate bounded-recursion case verifies that stack auto-grow itself
 * still works within the cap.
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int fails = 0;

static void check(int cond, const char *name) {
    if (cond) {
        printf("[stack_overflow_test] PASS  %s\n", name);
    } else {
        printf("[stack_overflow_test] FAIL  %s\n", name);
        fails++;
    }
}

/* Indirect call through a volatile function pointer defeats the compiler's
 * infinite-recursion detection. Post-call sink write (sink = burn[0])
 * defeats tail-call optimization so each frame actually consumes stack. */
static void unbounded_recurse(int depth);
static void (* volatile recurse_ptr)(int) = unbounded_recurse;
static volatile char sink;

static void unbounded_recurse(int depth) {
    volatile char burn[4096];
    burn[0] = (char)depth;
    burn[4095] = (char)depth;
    recurse_ptr(depth + 1);
    sink = burn[0];
}

static volatile int bounded_mark = 0;
static void bounded_recurse(int depth) {
    volatile char burn[1024];
    burn[0] = (char)depth;
    burn[1023] = (char)depth;
    (void)burn;
    if (depth >= 50) {
        bounded_mark = depth;
        return;
    }
    bounded_recurse(depth + 1);
}

int main(void) {
    printf("=== stack_overflow_test ===\n");

    /* Bounded recursion must succeed (stack auto-grow within cap). */
    bounded_recurse(0);
    check(bounded_mark == 50, "bounded recursion within cap succeeds");

    /* Unbounded recursion in child must be killed by SIGSEGV. */
    int pid = fork();
    if (pid < 0) {
        printf("[stack_overflow_test] FAIL  fork\n");
        return 1;
    }
    if (pid == 0) {
        unbounded_recurse(0);
        /* Must not reach — compiler must not optimize recursion away. */
        exit(0);
    }

    int st = -1;
    int got = wait(&st);
    check(got == pid, "waited for overflow child");
    check(st != 0, "child died (non-zero status)");
    check(st == 128 + SIGSEGV, "child died specifically with SIGSEGV");

    printf("=== stack_overflow_test: %d failures ===\n", fails);
    return fails;
}
