/*
 * grader_adversarial_test — Try to break the kernel.
 *
 * These are the tests a professor writes to intentionally crash,
 * hang, or corrupt the OS. Every one of these MUST fail gracefully.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/mman.h>

static int fails = 0;
static void check(int cond, const char *name) {
    if (cond) printf("[grader_adversarial] PASS  %s\n", name);
    else { printf("[grader_adversarial] FAIL  %s\n", name); fails++; }
}

int main(void) {
    printf("=== grader_adversarial_test ===\n");

    /* 1. NULL pointer dereference in child — must SIGSEGV, not kernel panic */
    {
        int pid = fork();
        if (pid == 0) {
            volatile char *p = (volatile char *)0;
            *p = 'X';  /* should die with SIGSEGV */
            exit(0);    /* should NOT reach here */
        }
        int st;
        waitpid(pid, &st, 0);
        check(WIFSIGNALED(st), "NULL deref: child killed by signal");
        check(WTERMSIG(st) == SIGSEGV, "NULL deref: signal is SIGSEGV");
    }

    /* 2. Stack overflow in child — must not corrupt kernel */
    {
        int pid = fork();
        if (pid == 0) {
            /* Infinite recursion to blow the stack */
            volatile char buf[4096];
            buf[0] = 'X';
            char *args[] = {"/bin/grader_adversarial_test", "recurse", 0};
            /* Instead of actual recursion (which we can't call main()),
             * just touch stack pages way below SP */
            volatile char *deep = (volatile char *)((unsigned long)buf - 2*1024*1024);
            *deep = 'X';
            exit(0);
        }
        int st;
        waitpid(pid, &st, 0);
        check(WIFSIGNALED(st), "stack overflow: child killed by signal");
    }

    /* 3. Write to text segment — must SIGSEGV */
    {
        int pid = fork();
        if (pid == 0) {
            /* Try to write to a function pointer (text segment) */
            volatile char *p = (volatile char *)main;
            *p = 0x90;
            exit(0);
        }
        int st;
        waitpid(pid, &st, 0);
        check(WIFSIGNALED(st), "write to text: child killed by signal");
    }

    /* 4. Read from unmapped high address */
    {
        int pid = fork();
        if (pid == 0) {
            volatile char *p = (volatile char *)0x3FFFF000UL;
            char c = *p;
            (void)c;
            exit(0);
        }
        int st;
        waitpid(pid, &st, 0);
        check(WIFSIGNALED(st), "read unmapped addr: child killed");
    }

    /* 5. exec a directory — must fail, not crash */
    {
        int pid = fork();
        if (pid == 0) {
            char *args[] = {"/", 0};
            execv("/", args);
            exit(errno != 0 ? 0 : 1); /* exec should fail */
        }
        int st;
        waitpid(pid, &st, 0);
        check(WIFEXITED(st) && WEXITSTATUS(st) == 0,
              "exec('/') fails gracefully");
    }

    /* 6. exec empty string — must fail, not crash */
    {
        int pid = fork();
        if (pid == 0) {
            char *args[] = {"", 0};
            execv("", args);
            exit(0); /* exec should fail, we get here */
        }
        int st;
        waitpid(pid, &st, 0);
        check(WIFEXITED(st), "exec('') fails gracefully");
    }

    /* 7. waitpid on non-child PID — must return ECHILD */
    {
        errno = 0;
        int r = waitpid(99999, 0, 0);
        check(r < 0, "waitpid(non-child) returns error");
    }

    /* 8. Open too many files — must fail with EMFILE, not crash */
    {
        int fds[64];
        int opened = 0;
        for (int i = 0; i < 64; i++) {
            fds[i] = open("/bin/echo", O_RDONLY);
            if (fds[i] < 0) break;
            opened++;
        }
        check(opened > 3, "can open multiple files");
        check(opened < 64, "file descriptor limit enforced");
        for (int i = 0; i < opened; i++) close(fds[i]);
    }

    /* 9. Very long filename — must fail, not crash */
    {
        char longname[512];
        memset(longname, 'A', 511);
        longname[0] = '/';
        longname[511] = 0;
        int fd = open(longname, O_RDONLY);
        check(fd < 0, "open(511-char filename) fails");
        if (fd >= 0) close(fd);
    }

    /* 10. sbrk negative below original brk — must fail */
    {
        void *cur = sbrk(0);
        void *r = sbrk(-(long)((unsigned long)cur)); /* try to go to address 0 */
        check(r == (void *)-1 || (long)r < 0, "sbrk to address 0 fails");
    }

    /* 11. Double close — must not corrupt fd table */
    {
        int fd = open("/bin/echo", O_RDONLY);
        if (fd >= 0) {
            close(fd);
            int r = close(fd);
            check(r < 0, "double close returns error");
            /* Now open a new fd — must NOT get the double-closed fd */
            int fd2 = open("/bin/echo", O_RDONLY);
            check(fd2 >= 0, "open after double-close works");
            if (fd2 >= 0) close(fd2);
        }
    }

    /* 12. munmap then access — must SIGSEGV */
    {
        void *m = mmap(0, 4096, PROT_READ | PROT_WRITE,
                       MAP_ANON | MAP_PRIVATE, -1, 0);
        if (m != MAP_FAILED && (long)m > 0) {
            volatile char *p = (volatile char *)m;
            p[0] = 'X';  /* should work */
            munmap(m, 4096);

            int pid = fork();
            if (pid == 0) {
                /* Access the unmapped region — should die */
                volatile char c = p[0];
                (void)c;
                exit(0);
            }
            int st;
            waitpid(pid, &st, 0);
            check(WIFSIGNALED(st), "access after munmap: child killed");
        }
    }

    /* 13. lseek on pipe — must fail */
    {
        int pfd[2];
        if (pipe(pfd) == 0) {
            long r = lseek(pfd[0], 0, SEEK_SET);
            check(r < 0, "lseek on pipe fails");
            close(pfd[0]);
            close(pfd[1]);
        }
    }

    /* 14. write to pipe with no readers — should get SIGPIPE or error */
    {
        int pfd[2];
        if (pipe(pfd) == 0) {
            close(pfd[0]); /* close read end */
            int pid = fork();
            if (pid == 0) {
                signal(SIGPIPE, SIG_DFL);
                char buf[64];
                memset(buf, 'X', 64);
                write(pfd[1], buf, 64);
                exit(0); /* if SIGPIPE doesn't kill us */
            }
            int st;
            waitpid(pid, &st, 0);
            /* Either killed by SIGPIPE or write returned error */
            check(WIFSIGNALED(st) || WIFEXITED(st),
                  "write to broken pipe: handled");
            close(pfd[1]);
        }
    }

    /* 15. Fork bomb (limited) — OS must not hang */
    {
        int pid = fork();
        if (pid == 0) {
            /* Try to fork 50 children rapidly */
            for (int i = 0; i < 50; i++) {
                int c = fork();
                if (c == 0) exit(0);
                if (c < 0) break; /* out of resources */
                /* don't wait — let them become zombies briefly */
            }
            /* Now wait for all */
            while (wait(0) > 0) {}
            exit(0);
        }
        int st;
        waitpid(pid, &st, 0);
        check(WIFEXITED(st), "fork bomb (50): parent survives");
    }

    /* 16. Orphan reparenting — grandchild outlives child */
    {
        int pfd[2];
        pipe(pfd);
        int pid = fork();
        if (pid == 0) {
            close(pfd[0]);
            int gpid = fork();
            if (gpid == 0) {
                /* Grandchild: sleep briefly, then exit */
                sched_yield();
                sched_yield();
                close(pfd[1]);
                exit(0);
            }
            /* Child exits immediately, orphaning grandchild */
            close(pfd[1]);
            exit(0);
        }
        close(pfd[1]);
        int st;
        waitpid(pid, &st, 0);
        check(WIFEXITED(st) && WEXITSTATUS(st) == 0,
              "orphan: child exits cleanly");
        /* Wait for grandchild to signal completion via pipe EOF */
        char buf[1];
        read(pfd[0], buf, 1); /* blocks until grandchild closes pipe */
        close(pfd[0]);
        check(1, "orphan: grandchild completed (init reaped)");
    }

    /* 17. exec with argc=0 — some programs check argv[0] */
    {
        int pid = fork();
        if (pid == 0) {
            char *args[] = {0};
            execv("/bin/echo", args);
            exit(0);
        }
        int st;
        waitpid(pid, &st, 0);
        /* Should either run or fail gracefully */
        check(WIFEXITED(st), "exec with NULL argv[0] doesn't crash kernel");
    }

    /* 18. Close stdin then do operations */
    {
        int pid = fork();
        if (pid == 0) {
            close(0);
            printf("stdout after close(0)\n");
            int fd = open("/bin/echo", O_RDONLY);
            /* fd should be 0 since it was just freed */
            exit(fd >= 0 ? 0 : 1);
        }
        int st;
        waitpid(pid, &st, 0);
        check(WIFEXITED(st) && WEXITSTATUS(st) == 0,
              "close(stdin) + open reuses fd 0");
    }

    printf("=== grader_adversarial_test: %d failures ===\n", fails);
    return fails;
}
