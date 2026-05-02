#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

int main(void) {
    printf("init: starting\n");

    char *tests[] = {
        /* libc surface tests — kept first so a libc regression fails
         * fast, before any of the longer-running kernel tests. */
        "/bin/wait_test",
        "/bin/setjmp_test",
        "/bin/ctype_test",
        "/bin/fork_test",
        "/bin/pid_test",
        "/bin/addrspace_test",
        "/bin/multi_fork_test",
        "/bin/write_test",
        "/bin/preempt_test",
        "/bin/segv_test",
        "/bin/sleep_test",
        "/bin/yield_test",
        "/bin/fd_test",
        "/bin/stat_test",
        "/bin/getdents_test",
        "/bin/chdir_test",
        "/bin/open_read_test",
        "/bin/dup_test",
        "/bin/getcwd_test",
        "/bin/fd_limits_test",
        "/bin/path_test",
        "/bin/sbfs_basic_test",
        "/bin/mkdir_test",
        "/bin/link_test",
        "/bin/rename_test",
        "/bin/pipe_test",
        "/bin/sbrk_test",
        "/bin/malloc_test",
        "/bin/cow_test",
        "/bin/mmap_test",
        "/bin/stack_test",
        "/bin/sbrk_edge_test",
        "/bin/cow_write_test",
        "/bin/munmap_test",
        "/bin/mmap_stress_test",
        "/bin/fork_storm_test",
        "/bin/pipe_stress_test",
        "/bin/exec_reset_test",
        "/bin/comprehensive_test",
        /* Phase 8a: time + uid/gid */
        "/bin/time_test",
        "/bin/date",
        "/bin/uid_test",
        /* Phase 8b/8c: signals + termios */
        "/bin/signal_test",
        "/bin/sigchld_test",
        "/bin/sigpipe_test",
        "/bin/sigsegv_handler_test",
        "/bin/eintr_test",
        "/bin/termios_test",
        "/bin/copyio_test",
        "/bin/copyio_fuzz_test",
        "/bin/exec_argv_test",
        "/bin/fd_invariant_test",
        "/bin/vma_overlap_test",
        "/bin/rlimit_test",
        "/bin/oom_test",
        "/bin/leak_test",
        "/bin/reap_stress_test",
        "/bin/resource_churn_test",
        /* Phase 7b: lazy allocation + demand paging */
        "/bin/lazy_sbrk_test",
        "/bin/zerofill_test",
        "/bin/demand_walk_test",
        "/bin/stack_overflow_test",
        "/bin/symlink_test",
        "/bin/proc_test",
        "/bin/ps",
        "/bin/header_test",
        "/bin/headers_compile_gate",
        "/bin/sh_c_test",
        "/bin/usertests",
    };
    int ntests = (int)(sizeof(tests) / sizeof(tests[0]));

    int pass = 0, fail = 0;
    for (int i = 0; i < ntests; i++) {
        int pid = fork();
        if (pid == 0) {
            execv(tests[i], 0);
            printf("init: exec '%s' failed\n", tests[i]);
            exit(1);
        }
        int status = -1;
        while (1) {
            int got = wait(&status);
            if (got == pid) break;
            if (got == -1 && errno == EINTR) continue;
            if (got < 0) {
                status = 1;
                break;
            }
        }
        if (status == 0) {
            pass++;
        } else {
            printf("init: '%s' exited with status %d\n", tests[i], status);
            fail++;
        }
    }
    printf("init: %d/%d tests passed\n", pass, ntests);

    while (1) {
        printf("Starting /bin/sh\n");
        int pid = fork();
        if (pid == 0) {
            execv("/bin/sh", 0);
            printf("init: exec /bin/sh failed\n");
            exit(1);
        }
        while (1) {
            int got = wait(0);
            if (got == pid) break;
            if (got == -1 && errno == EINTR) continue;
            if (got < 0) break;
        }
    }
}
