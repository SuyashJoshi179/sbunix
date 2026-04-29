#include <stdio.h>
#include <setjmp.h>

static jmp_buf env;

/* A function call between setjmp() and longjmp() forces s0..s11 to matter:
 * the inner frame's callee-saves get restored by longjmp's pop. */
static void deep(int v) {
    volatile int local = 0xC0DE;
    (void)local;
    longjmp(env, v);
}

int main(void) {
    /* Round 1: longjmp(env, 42) → setjmp returns 42. */
    int r = setjmp(env);
    if (r == 0) {
        deep(42);
        printf("setjmp_test: longjmp returned through setjmp\n");
        return 1;
    }
    if (r != 42) {
        printf("setjmp_test: round 1 bad value %d (want 42)\n", r);
        return 1;
    }

    /* Round 2: longjmp(env, 0) must cause setjmp to return 1, per C99. */
    volatile int phase = 0;
    int r2 = setjmp(env);
    if (phase == 0) {
        phase = 1;
        longjmp(env, 0);
    }
    if (r2 != 1) {
        printf("setjmp_test: round 2 longjmp(.,0) returned %d (want 1)\n", r2);
        return 1;
    }

    /* Round 3: longjmp with a large non-zero value passes through verbatim. */
    volatile int phase3 = 0;
    int r3 = setjmp(env);
    if (phase3 == 0) {
        phase3 = 1;
        longjmp(env, 12345);
    }
    if (r3 != 12345) {
        printf("setjmp_test: round 3 returned %d (want 12345)\n", r3);
        return 1;
    }

    printf("setjmp_test: PASS\n");
    return 0;
}
