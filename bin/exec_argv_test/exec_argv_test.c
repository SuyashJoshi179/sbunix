#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define PAGE_SZ 4096UL
#define KERNEL_BASE_VA 0xFFFFFFFF00000000UL

static int run_child_expect_status0(int (*fn)(void), const char *name) {
    int pid = fork();
    if (pid < 0) {
        printf("exec_argv_test: fork failed for %s\n", name);
        return 0;
    }
    if (pid == 0)
        exit(fn() ? 0 : 1);

    int st = -1;
    while (1) {
        int got = wait(&st);
        if (got == pid) break;
        if (got == -EINTR) continue;
        if (got < 0) return 0;
    }
    return st == 0;
}

static int case_valid_exec(void) {
    int pid = fork();
    if (pid < 0) return 0;
    if (pid == 0) {
        char *argv[] = {"echo", "exec_argv_ok", 0};
        execv("/bin/echo", argv);
        exit(2);
    }

    int st = -1;
    while (1) {
        int got = wait(&st);
        if (got == pid) break;
        if (got == -EINTR) continue;
        if (got < 0) return 0;
    }
    return st == 0;
}

static int case_bad_path_kernel_va(void) {
    char *argv[] = {"echo", 0};
    long r = execv((const char *)(uintptr_t)KERNEL_BASE_VA, argv);
    return r < 0;
}

static int case_bad_argv_kernel_va(void) {
    long r = execv("/bin/echo", (char *const *)(uintptr_t)KERNEL_BASE_VA);
    return r < 0;
}

static int case_straddle_argv_ptr(void) {
    char *r = mmap(0, 2 * PAGE_SZ, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if ((long)r < 0) return 0;
    if (munmap(r + PAGE_SZ, PAGE_SZ) < 0) return 0;

    uint64_t *vec = (uint64_t *)(r + PAGE_SZ - 4);
    long rv = execv("/bin/echo", (char *const *)vec);
    return rv < 0;
}

static int case_unterminated_arg(void) {
    char *r = mmap(0, PAGE_SZ, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if ((long)r < 0) return 0;
    for (int i = 0; i < (int)PAGE_SZ; i++)
        r[i] = 'A';

    char *vec[2];
    vec[0] = r;
    vec[1] = 0;

    long rv = execv("/bin/echo", vec);
    return rv < 0;
}

int main(void) {
    int pass = 0;
    int fail = 0;

    if (run_child_expect_status0(case_valid_exec, "valid_exec")) pass++; else fail++;
    if (run_child_expect_status0(case_bad_path_kernel_va, "bad_path_kernel_va")) pass++; else fail++;
    if (run_child_expect_status0(case_bad_argv_kernel_va, "bad_argv_kernel_va")) pass++; else fail++;
    if (run_child_expect_status0(case_straddle_argv_ptr, "straddle_argv_ptr")) pass++; else fail++;
    if (run_child_expect_status0(case_unterminated_arg, "unterminated_arg")) pass++; else fail++;

    printf("exec_argv_test: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
