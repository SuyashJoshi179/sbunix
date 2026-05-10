#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>

int main(void) {
    printf("init: starting\n");

    /* Become session leader and claim the console. Children fork into
     * their own pgrps; the foreground pgrp is handed off via tcsetpgrp
     * so signal chars (^C/^Z/^\) only hit the running test. */
    setsid();
    int shell_pgid = getpgrp();
    ioctl(0, 0x5410 /* TIOCSPGRP */, &shell_pgid);

    /* Run /etc/rc once at boot — mounts /proc, /mnt, /tmp.
     *
     * On develop the selftest pre-attaches /proc and /mnt, so these
     * calls hit mount_fs idempotency and return 0. On release builds
     * (selftest stripped) /etc/rc is the *only* path that mounts these
     * filesystems for userspace.
     *
     * !!! DO NOT REMOVE — required on every build. The strip markers
     * below delimit only the test block; this fork must survive. !!! */
    {
        int rc_pid = fork();
        if (rc_pid == 0) {
            char *args[] = {"/bin/sh", "/etc/rc", 0};
            execv("/bin/sh", args);
            printf("init: exec /bin/sh /etc/rc failed\n");
            exit(1);
        }
        if (rc_pid > 0) {
            int rc_st;
            while (1) {
                int got = wait(&rc_st);
                if (got == rc_pid) break;
                if (got < 0 && errno != EINTR) break;
            }
        }
    }

    /* ============================================================
     * STRIP-BEGIN: TESTS
     * Release-branch strip script removes everything between this
     * marker and STRIP-END. The /etc/rc fork above and the shell
     * loop below MUST survive the strip.
     * ============================================================ */
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
        "/bin/timestamp_test",
        "/bin/tmpfs_test",
        "/bin/pipe_test",
        "/bin/sbrk_test",
        "/bin/malloc_test",
        /* Large-userspace-allocations feature tests (run early so they
         * surface before the slow OOM/leak suite). */
        "/bin/setrlimit_test",
        "/bin/free_reuse",
        "/bin/direct_threshold",
        "/bin/mmap_fixed_test",
        "/bin/stack_grow_test",
        "/bin/many_mmaps_test",
        "/bin/pattern_write",
        "/bin/lazy_reserve",
        "/bin/huge_reserve_no_touch",
        "/bin/exec_resets_arena",
        "/bin/multi_alloc_disjoint",
        "/bin/realloc_grow",
        "/bin/calloc_zero",
        "/bin/free_coalesce",
        "/bin/free_then_huge",
        "/bin/mmap_anon_huge",
        "/bin/fork_cow_heap",
        "/bin/rlimit_inherit",
        "/bin/nohuge_oom",
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
        "/bin/time_posix_test",
        "/bin/date",
        "/bin/uid_test",
        /* Phase 8b/8c: signals + termios */
        "/bin/pgrp_test",
        "/bin/kill_pgrp_test",
        "/bin/sigtstp_test",
        "/bin/wait4_nohang_test",
        "/bin/signal_test",
        "/bin/sigmask_test",
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
        "/bin/bss_test",
        "/bin/demand_walk_test",
        "/bin/stack_overflow_test",
        "/bin/symlink_test",
        "/bin/proc_test",
        "/bin/ps",
        "/bin/header_test",
        "/bin/headers_compile_gate",
        "/bin/sh_c_test",
        "/bin/usertests",
        "/bin/mmap_smoke_test",
        "/bin/mmap_cow_test",
        "/bin/pagecache_test",
        /* Phase D follow-ups (see docs/superpowers/specs/2026-05-03-page-cache-design.md):
         *   /bin/truncate_mmap_test  — open(O_TRUNC) on already-mapped file
         *   /bin/mmap_share_test     — fork-shared MAP_SHARED visibility
         *   /bin/bigfile_pcache_test — needs file > sbfs size cap
         *   /bin/pagecache_stress    — write-while-mapped coherence */
    };
    int ntests = (int)(sizeof(tests) / sizeof(tests[0]));

    int pass = 0, fail = 0;
    for (int i = 0; i < ntests; i++) {
        int pid = fork();
        if (pid == 0) {
            setpgid(0, 0);
            execv(tests[i], 0);
            printf("init: exec '%s' failed\n", tests[i]);
            exit(1);
        }
        setpgid(pid, pid);
        ioctl(0, 0x5410 /* TIOCSPGRP */, &pid);
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
        ioctl(0, 0x5410 /* TIOCSPGRP */, &shell_pgid);
        if (status == 0) {
            pass++;
        } else {
            printf("init: '%s' exited with status %d\n", tests[i], status);
            fail++;
        }
    }
    printf("init: %d/%d tests passed\n", pass, ntests);
    /* ============================================================
     * STRIP-END: TESTS
     * ============================================================ */

    while (1) {
        printf("Starting /bin/sh\n");
        int pid = fork();
        if (pid == 0) {
            setpgid(0, 0);
            execv("/bin/sh", 0);
            printf("init: exec /bin/sh failed\n");
            exit(1);
        }
        setpgid(pid, pid);
        ioctl(0, 0x5410 /* TIOCSPGRP */, &pid);
        while (1) {
            int got = wait(0);
            if (got == pid) break;
            if (got == -1 && errno == EINTR) continue;
            if (got < 0) break;
        }
        ioctl(0, 0x5410 /* TIOCSPGRP */, &shell_pgid);
    }
}
