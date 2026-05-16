#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

/* Boot-time regression suite. Invoked from /etc/rc before the
 * interactive shell so that adding a new test only requires editing
 * this file (or registering via the tests[] array). Lives in its own
 * binary, separate from /bin/init, so /etc/rc can keep the prof's
 * conventional `exec /bin/sh` at the end without blocking the test
 * runner.
 *
 * Job control: each test runs in its own pgrp via setpgid; the
 * foreground pgrp is handed off via tcsetpgrp so signal chars
 * (^C/^Z/^\) only hit the running test. After each test the
 * foreground pgrp is restored to our own pgrp (which is sh's pgrp
 * when invoked from rc). */
int main(void) {
    int parent_pgid = getpgrp();

    char *tests[] = {
        "/bin/wait_test",
        "/bin/setjmp_test",
        "/bin/ctype_test",
        "/bin/strtol_overflow_test",
        "/bin/mntent_test",
        "/bin/strftime_c_test",
        "/bin/o_append_test",
        "/bin/scandir_test",
        "/bin/atexit_test",
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
        "/bin/tarfs_dots_test",
        "/bin/chdir_test",
        "/bin/open_read_test",
        "/bin/o_excl_test",
        "/bin/dup_test",
        "/bin/cloexec_test",
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
        "/bin/partial_munmap_test",
        "/bin/mmap_stress_test",
        "/bin/fork_storm_test",
        "/bin/pipe_stress_test",
        "/bin/exec_reset_test",
        "/bin/comprehensive_test",
        "/bin/time_test",
        "/bin/time_posix_test",
        "/bin/date",
        "/bin/uid_test",
        "/bin/pgrp_test",
        "/bin/kill_pgrp_test",
        "/bin/sigtstp_test",
        "/bin/wait4_nohang_test",
        "/bin/signal_test",
        "/bin/sigmask_test",
        "/bin/sigchld_test",
        "/bin/sigpipe_test",
        "/bin/sigsegv_handler_test",
        "/bin/sigaltstack_test",
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
        "/bin/select_test",
        "/bin/pread_pwrite_test",
        "/bin/execve_env_test",
        "/bin/utimes_test",
        "/bin/openat_test",
        "/bin/stat_direct_test",
        "/bin/mkfifo_test",
        "/bin/at_family_test",
        "/bin/poll_test",
        "/bin/execvp_path_test",
        "/bin/fchdir_test",
        "/bin/getrandom_test",
        "/bin/sh_hardening_test",
        "/bin/usertests",
        "/bin/mmap_smoke_test",
        "/bin/mmap_cow_test",
        "/bin/cow_pcache_refleak_test",
        "/bin/pagecache_test",
    };
    int ntests = (int)(sizeof(tests) / sizeof(tests[0]));

    int pass = 0, fail = 0;
    for (int i = 0; i < ntests; i++) {
        int pid = fork();
        if (pid == 0) {
            setpgid(0, 0);
            execv(tests[i], 0);
            printf("runtests: exec '%s' failed\n", tests[i]);
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
        ioctl(0, 0x5410 /* TIOCSPGRP */, &parent_pgid);
        if (status == 0) {
            pass++;
        } else {
            printf("runtests: '%s' exited with status %d\n", tests[i], status);
            fail++;
        }
    }
    printf("init: %d/%d tests passed\n", pass, ntests);
    return fail == 0 ? 0 : 1;
}
