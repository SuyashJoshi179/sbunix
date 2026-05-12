#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

int main(void) {
    printf("init: starting\n");

    /* Become session leader and claim the console. Children fork into
     * their own pgrps; the foreground pgrp is handed off via tcsetpgrp
     * so signal chars (^C/^Z/^\) only hit the running test. */
    setsid();
    int shell_pgid = getpgrp();
    ioctl(0, 0x5410 /* TIOCSPGRP */, &shell_pgid);

    /* Run /etc/rc once at boot — prof's rc invokes `mount -t proc … /proc`
     * and `mount -t disk … /mnt`. Selftest already pre-attached both, so
     * these calls hit mount_fs idempotency and return 0. Failures are
     * non-fatal: tests below still run. */
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

    char *tests[] = {
        /* libc surface tests — kept first so a libc regression fails
         * fast, before any of the longer-running kernel tests. */
        "/bin/wait_test",
        "/bin/setjmp_test",
        "/bin/ctype_test",
        "/bin/strtol_overflow_test",
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
        "/bin/dev_zero_tty_test",
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
        "/bin/sa_restart_test",
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
        "/bin/env_test",
        "/bin/sh_c_test",
        "/bin/usertests",
        "/bin/mmap_smoke_test",
        "/bin/mmap_cow_test",
        "/bin/cow_pcache_refleak_test",
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

    int sh_failures = 0;
    while (1) {
        /* Reap any orphans whose original parents have already exited.
         * Without this, detached subprocesses become permanent zombies
         * because nobody else calls wait() on them. */
        while (waitpid(-1, 0, WNOHANG) > 0) { }

        printf("Starting /bin/sh\n");
        int pid = fork();
        if (pid == 0) {
            setpgid(0, 0);
            char *sh_args[] = { "/bin/sh", 0 };
            execv("/bin/sh", sh_args);
            printf("init: exec /bin/sh failed\n");
            exit(127);
        }
        setpgid(pid, pid);
        ioctl(0, 0x5410 /* TIOCSPGRP */, &pid);
        int sh_status = 0;
        while (1) {
            int got = wait(&sh_status);
            if (got == pid) break;
            if (got == -1 && errno == EINTR) continue;
            if (got < 0) break;
        }
        ioctl(0, 0x5410 /* TIOCSPGRP */, &shell_pgid);

        /* Backoff: a shell that exec-fails or crashes within ~1 tick
         * means there's no recoverable shell to spawn. Without backoff
         * init busy-loops forking and the kernel log fills with
         * "Starting /bin/sh ... init: exec /bin/sh failed" instantly. */
        if (sh_status == 127) {
            sh_failures++;
            if (sh_failures >= 5) {
                printf("init: /bin/sh fails repeatedly; sleeping 5s\n");
                sleep(5);
                sh_failures = 0;
            }
        } else {
            sh_failures = 0;
        }
    }
}
