/*
 * usertests — SBUnix analogue of xv6's usertests.c.
 *
 * Single-binary regression suite. Exercises the user/kernel ABI from many
 * angles: file I/O, directory ops, fork/wait/exec, pipes, shared fds, mem,
 * sbrk/mmap, and signals. Each sub-test prints PASS/FAIL; the process exits
 * with the number of failures.
 *
 * Adapted from xv6's usertests.c (MIT/BSD) — restructured for SBUnix's
 * POSIX-shaped syscalls (wait(&st), execv, O_CREAT, exit(int), kill(pid,sig)).
 * Skipped features: link(), fstat on dirs by size, bss relocations,
 * validate-bad-address (copyio_fuzz already covers that extensively).
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define PAGE_SIZE 4096

static int fails = 0;
static char buf[8192];

static void chk(int cond, const char *name) {
    if (cond) {
        printf("[usertests] PASS  %s\n", name);
    } else {
        printf("[usertests] FAIL  %s\n", name);
        fails++;
    }
}

static int wait_status(int want_pid) {
    int st = -1;
    int got = wait(&st);
    if (got != want_pid) return -1;
    return st;
}

/* ---------- fs: open / create / read / write / unlink ---------- */

static void opentest(void) {
    int fd = open("/bin/init", O_RDONLY);
    chk(fd >= 0, "open existing /bin/init");
    if (fd >= 0) close(fd);

    fd = open("/does/not/exist", O_RDONLY);
    chk(fd < 0, "open nonexistent fails");
}

static void writetest_small(void) {
    unlink("/data/small");
    int fd = open("/data/small", O_RDWR | O_CREAT);
    chk(fd >= 0, "create /data/small");
    if (fd < 0) return;

    int ok = 1;
    for (int i = 0; i < 100; i++) {
        if (write(fd, "aaaaaaaaaa", 10) != 10) { ok = 0; break; }
        if (write(fd, "bbbbbbbbbb", 10) != 10) { ok = 0; break; }
    }
    chk(ok, "write 100x(a10+b10) to small");
    close(fd);

    fd = open("/data/small", O_RDONLY);
    chk(fd >= 0, "reopen small for read");
    long n = read(fd, buf, sizeof(buf));
    chk(n == 2000, "small: read 2000 bytes back");
    int content_ok = 1;
    for (int i = 0; i < 2000 && content_ok; i++) {
        char want = (i / 10) % 2 == 0 ? 'a' : 'b';
        if (buf[i] != want) content_ok = 0;
    }
    chk(content_ok, "small: content matches written pattern");
    close(fd);
    chk(unlink("/data/small") == 0, "unlink /data/small");
}

static void createtest_many(void) {
    char name[] = "/data/aX";
    int ok = 1;
    for (int i = 0; i < 20; i++) {
        name[7] = 'a' + i;
        int fd = open(name, O_RDWR | O_CREAT);
        if (fd < 0) { ok = 0; break; }
        close(fd);
    }
    chk(ok, "create 20 files in /data");
    int unlinked = 0;
    for (int i = 0; i < 20; i++) {
        name[7] = 'a' + i;
        if (unlink(name) == 0) unlinked++;
    }
    chk(unlinked == 20, "unlink all 20");
}

static void unlinkread(void) {
    unlink("/data/unlinkread");
    int fd = open("/data/unlinkread", O_RDWR | O_CREAT);
    chk(fd >= 0, "create /data/unlinkread");
    if (fd < 0) return;
    write(fd, "hello", 5);
    close(fd);

    fd = open("/data/unlinkread", O_RDONLY);
    chk(fd >= 0, "reopen before unlink");
    chk(unlink("/data/unlinkread") == 0, "unlink while fd open");
    char b[8] = {0};
    long n = read(fd, b, 5);
    chk(n == 5 && b[0] == 'h' && b[4] == 'o', "read from unlinked-but-open fd");
    close(fd);

    int fd2 = open("/data/unlinkread", O_RDONLY);
    chk(fd2 < 0, "reopen of unlinked file fails");
}

/* ---------- dirs ---------- */

static void dirtest(void) {
    chk(mkdir("/data/dir0", 0755) == 0, "mkdir /data/dir0");
    chk(chdir("/data/dir0") == 0, "chdir into /data/dir0");
    chk(chdir("/") == 0, "chdir back to /");
    chk(unlink("/data/dir0") == 0, "unlink empty dir");
}

static void subdir(void) {
    mkdir("/data/sub", 0755);
    mkdir("/data/sub/a", 0755);
    int fd = open("/data/sub/a/f", O_RDWR | O_CREAT);
    chk(fd >= 0, "create nested /data/sub/a/f");
    if (fd >= 0) {
        chk(write(fd, "xyz", 3) == 3, "write to nested file");
        close(fd);
    }
    fd = open("/data/sub/a/f", O_RDONLY);
    chk(fd >= 0, "reopen nested file");
    if (fd >= 0) {
        char b[4] = {0};
        chk(read(fd, b, 3) == 3 && b[0] == 'x' && b[2] == 'z', "nested content OK");
        close(fd);
    }
    unlink("/data/sub/a/f");
    unlink("/data/sub/a");
    unlink("/data/sub");
}

/* ---------- fork / wait / exec ---------- */

static void exitwait_stress(void) {
    int ok = 1;
    for (int i = 0; i < 50; i++) {
        int pid = fork();
        if (pid < 0) { ok = 0; break; }
        if (pid == 0) exit(i & 0x7F);
        int st = -1;
        int got = wait(&st);
        if (got != pid || !WIFEXITED(st) || WEXITSTATUS(st) != (i & 0x7F)) { ok = 0; break; }
    }
    chk(ok, "50x fork/exit/wait, status preserved");
}

static void forktest_many(void) {
    int N = 20;
    int ok = 1;
    for (int i = 0; i < N; i++) {
        int pid = fork();
        if (pid < 0) { ok = 0; break; }
        if (pid == 0) exit(0);
    }
    int reaped = 0;
    for (int i = 0; i < N; i++) {
        int st = -1;
        if (wait(&st) > 0) reaped++;
    }
    chk(ok && reaped == N, "fork 20 kids, all reaped");
}

static void exectest_echo(void) {
    int pid = fork();
    chk(pid >= 0, "fork for exec");
    if (pid == 0) {
        char *argv[] = {"/bin/echo", "usertests-exec-marker", 0};
        execv("/bin/echo", argv);
        exit(127);
    }
    int st = wait_status(pid);
    chk(st == 0, "/bin/echo exec exits 0");
}

/* ---------- pipes ---------- */

static void pipe1(void) {
    int fds[2];
    chk(pipe(fds) == 0, "pipe()");
    int pid = fork();
    if (pid == 0) {
        close(fds[0]);
        int seq = 0;
        for (int n = 0; n < 5; n++) {
            for (int i = 0; i < 1033; i++) buf[i] = seq++;
            if (write(fds[1], buf, 1033) != 1033) exit(1);
        }
        exit(0);
    }
    close(fds[1]);
    int total = 0, seq = 0, cc = 1;
    long n;
    int ok = 1;
    while ((n = read(fds[0], buf, cc)) > 0) {
        for (long i = 0; i < n; i++)
            if ((buf[i] & 0xff) != (seq++ & 0xff)) { ok = 0; break; }
        total += n;
        cc *= 2;
        if (cc > (int)sizeof(buf)) cc = sizeof(buf);
    }
    chk(ok && total == 5 * 1033, "pipe1: 5x1033 bytes round-trip");
    close(fds[0]);
    wait(0);
}

static void pipe_eof(void) {
    int fds[2];
    pipe(fds);
    int pid = fork();
    if (pid == 0) {
        close(fds[0]);
        write(fds[1], "hi", 2);
        close(fds[1]);
        exit(0);
    }
    close(fds[1]);
    char b[8] = {0};
    long n = read(fds[0], b, 8);
    chk(n == 2 && b[0] == 'h', "pipe: read partial from writer");
    n = read(fds[0], b, 8);
    chk(n == 0, "pipe: read returns 0 after writer close (EOF)");
    close(fds[0]);
    wait(0);
}

/* ---------- shared fd via fork ---------- */

static void sharedfd(void) {
    unlink("/data/sharedfd");
    int fd = open("/data/sharedfd", O_RDWR | O_CREAT);
    chk(fd >= 0, "open shared fd");
    if (fd < 0) return;
    int pid = fork();
    char c = (pid == 0) ? 'c' : 'p';
    for (int i = 0; i < sizeof(buf); i++) buf[i] = c;
    int ok = 1;
    for (int i = 0; i < 100; i++) {
        if (write(fd, buf, 10) != 10) { ok = 0; break; }
    }
    if (pid == 0) exit(ok ? 0 : 1);
    int st = wait_status(pid);
    chk(ok && st == 0, "sharedfd: both procs wrote 1000 bytes");
    close(fd);

    fd = open("/data/sharedfd", O_RDONLY);
    int nc = 0, np = 0;
    long n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (long i = 0; i < n; i++) {
            if (buf[i] == 'c') nc++;
            else if (buf[i] == 'p') np++;
        }
    }
    close(fd);
    chk(nc == 1000 && np == 1000, "sharedfd: 1000 c + 1000 p (shared offset)");
    unlink("/data/sharedfd");
}

/* ---------- mem: malloc stress until failure ---------- */

static void mem_stress(void) {
    int pid = fork();
    if (pid == 0) {
        void *head = 0;
        int count = 0;
        for (;;) {
            void *m = malloc(10001);
            if (!m) break;
            *(void **)m = head;
            head = m;
            count++;
            if (count > 1000) break;
        }
        while (head) {
            void *next = *(void **)head;
            free(head);
            head = next;
        }
        void *big = malloc(20 * 1024);
        if (!big) exit(2);
        free(big);
        exit(0);
    }
    int st = wait_status(pid);
    chk(st == 0, "mem: alloc-until-full then free, re-alloc 20K");
}

/* ---------- sbrk ---------- */

static void sbrk_basic(void) {
    void *a = sbrk(0);
    void *b = sbrk(4096);
    chk(a == b, "sbrk(4096) returns old brk");
    void *c = sbrk(0);
    chk((long)c - (long)b == 4096, "brk advanced by 4096");
    volatile char *p = (volatile char *)b;
    p[0] = 'x';
    p[4095] = 'y';
    chk(p[0] == 'x' && p[4095] == 'y', "written page readable");
    sbrk(-4096);
}

/* ---------- mmap anon ---------- */

static void mmap_anon(void) {
    volatile char *m = mmap(0, 8 * PAGE_SIZE, PROT_READ | PROT_WRITE,
                            MAP_ANON | MAP_PRIVATE, -1, 0);
    chk((long)m > 0, "mmap 8 pages anon");
    m[0] = 1;
    m[7 * PAGE_SIZE] = 7;
    chk(m[0] == 1 && m[7 * PAGE_SIZE] == 7, "mmap pages writable");
    chk(munmap((void *)m, 8 * PAGE_SIZE) == 0, "munmap 8 pages");
}

/* ---------- signals: SIGUSR1 handler delivered across fork ---------- */

static volatile int sig_hits = 0;
static void usr1_handler(int s) { (void)s; sig_hits++; }

static void signal_basic(void) {
    sig_hits = 0;
    signal(SIGUSR1, usr1_handler);
    raise(SIGUSR1);
    chk(sig_hits == 1, "SIGUSR1 handler fires on raise()");
    signal(SIGUSR1, SIG_DFL);
}

static void sigpipe_writer(void) {
    signal(SIGPIPE, SIG_IGN);
    int fds[2];
    pipe(fds);
    close(fds[0]);
    long n = write(fds[1], "x", 1);
    chk(n < 0, "write to pipe with no reader fails (SIGPIPE ignored)");
    close(fds[1]);
    signal(SIGPIPE, SIG_DFL);
}

/* ---------- dup / dup2 ---------- */

static void duptest(void) {
    unlink("/data/dup");
    int fd = open("/data/dup", O_RDWR | O_CREAT);
    chk(fd >= 0, "open /data/dup");
    int fd2 = dup(fd);
    chk(fd2 >= 0 && fd2 != fd, "dup() returns new fd");
    write(fd, "AB", 2);
    write(fd2, "CD", 2);
    close(fd);
    close(fd2);
    fd = open("/data/dup", O_RDONLY);
    char b[8] = {0};
    long n = read(fd, b, 4);
    chk(n == 4 && b[0] == 'A' && b[1] == 'B' && b[2] == 'C' && b[3] == 'D',
        "dup'd fds share offset (ABCD)");
    close(fd);
    unlink("/data/dup");
}

/* ---------- concurrent creates: 4 procs create distinct files ---------- */

static void fourfiles(void) {
    const char *names[] = {"/data/f0", "/data/f1", "/data/f2", "/data/f3"};
    for (int i = 0; i < 4; i++) unlink(names[i]);
    for (int pi = 0; pi < 4; pi++) {
        if (fork() == 0) {
            int fd = open(names[pi], O_RDWR | O_CREAT);
            if (fd < 0) exit(1);
            memset(buf, '0' + pi, 500);
            for (int i = 0; i < 10; i++) {
                if (write(fd, buf, 500) != 500) exit(2);
            }
            close(fd);
            exit(0);
        }
    }
    int all_ok = 1;
    for (int i = 0; i < 4; i++) {
        int st = -1;
        int got = wait(&st);
        if (got < 0 || st != 0) all_ok = 0;
    }
    chk(all_ok, "fourfiles: 4 procs each wrote 5000 bytes");

    int content_ok = 1;
    for (int i = 0; i < 4; i++) {
        int fd = open(names[i], O_RDONLY);
        if (fd < 0) { content_ok = 0; break; }
        long total = 0, n;
        while ((n = read(fd, buf, sizeof(buf))) > 0) {
            for (long j = 0; j < n; j++)
                if (buf[j] != '0' + i) { content_ok = 0; break; }
            total += n;
        }
        close(fd);
        if (total != 5000) content_ok = 0;
        unlink(names[i]);
    }
    chk(content_ok, "fourfiles: each file's content/length correct");
}

/* ---------- big write ---------- */

static void bigwrite(void) {
    unlink("/data/bigw");
    int fd = open("/data/bigw", O_RDWR | O_CREAT);
    if (fd < 0) { chk(0, "open bigw"); return; }
    int ok = 1;
    for (int sz = 499; sz < 8192; sz += 471) {
        long n = write(fd, buf, sz);
        if (n != sz) { ok = 0; break; }
    }
    close(fd);
    chk(ok, "bigwrite: variable-size writes accepted");
    unlink("/data/bigw");
}

/* ---------- bad fd / bad syscall args ---------- */

static void badargs(void) {
    chk(close(999) < 0, "close bad fd fails");
    chk(read(999, buf, 10) < 0, "read bad fd fails");
    chk(write(999, buf, 10) < 0, "write bad fd fails");
    chk(dup(999) < 0, "dup bad fd fails");
}

/* ---------- cow across fork: child write must not affect parent ---------- */

static void cow_independence(void) {
    static char page[PAGE_SIZE];
    memset(page, 'P', PAGE_SIZE);
    int pid = fork();
    if (pid == 0) {
        memset(page, 'C', PAGE_SIZE);
        if (page[0] != 'C' || page[PAGE_SIZE - 1] != 'C') exit(1);
        exit(0);
    }
    int st = wait_status(pid);
    chk(st == 0, "child wrote page to 'C'");
    chk(page[0] == 'P' && page[PAGE_SIZE - 1] == 'P',
        "parent page unchanged (COW isolation)");
}

/* ---------- getpid / getppid ---------- */

static void pidppid(void) {
    int my = getpid();
    chk(my > 0, "getpid > 0");
    int pid = fork();
    if (pid == 0) {
        int ppid = getppid();
        exit(ppid == my ? 0 : 1);
    }
    int st = wait_status(pid);
    chk(st == 0, "child's getppid == parent pid");
}

/* ---------- lseek ---------- */

static void lseektest(void) {
    unlink("/data/lseek");
    int fd = open("/data/lseek", O_RDWR | O_CREAT);
    if (fd < 0) { chk(0, "open lseek file"); return; }
    write(fd, "0123456789", 10);
    chk(lseek(fd, 3, 0) == 3, "lseek SEEK_SET 3");
    char b[4] = {0};
    chk(read(fd, b, 3) == 3 && b[0] == '3' && b[2] == '5',
        "read after lseek returns offset data");
    close(fd);
    unlink("/data/lseek");
}

/* ---------- orphan reaping: parent exits before child ---------- */

static void orphan(void) {
    int pid = fork();
    if (pid == 0) {
        int gc = fork();
        if (gc == 0) {
            usleep(20 * 1000);
            exit(0);
        }
        exit(0);
    }
    int st = wait_status(pid);
    chk(st == 0, "parent of orphan exits cleanly");
    usleep(100 * 1000);
}

/* ---------- main ---------- */

int main(void) {
    printf("=== usertests ===\n");

    printf("-- fs basics --\n");
    opentest();
    writetest_small();
    createtest_many();
    unlinkread();
    lseektest();

    printf("-- dirs --\n");
    dirtest();
    subdir();

    printf("-- fork / exec / wait --\n");
    exitwait_stress();
    forktest_many();
    exectest_echo();
    pidppid();
    orphan();

    printf("-- pipes --\n");
    pipe1();
    pipe_eof();

    printf("-- shared state --\n");
    sharedfd();
    duptest();
    fourfiles();
    bigwrite();

    printf("-- memory --\n");
    mem_stress();
    sbrk_basic();
    mmap_anon();
    cow_independence();

    printf("-- signals --\n");
    signal_basic();
    sigpipe_writer();

    printf("-- bad args --\n");
    badargs();

    printf("=== usertests: %d failures ===\n", fails);
    return fails;
}
