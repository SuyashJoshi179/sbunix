#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int fails = 0;

static void check(int cond, const char *name) {
    if (cond) {
        printf("[proc_test] PASS  %s\n", name);
    } else {
        printf("[proc_test] FAIL  %s\n", name);
        fails++;
    }
}

static int contains(const char *hay, const char *needle) {
    int hlen = (int)strlen(hay);
    int nlen = (int)strlen(needle);
    for (int i = 0; i + nlen <= hlen; i++) {
        int j = 0;
        while (j < nlen && hay[i + j] == needle[j]) j++;
        if (j == nlen) return 1;
    }
    return 0;
}

static int append_uint(char *buf, int off, unsigned int v) {
    char tmp[16];
    int n = 0;

    if (v == 0) {
        buf[off++] = '0';
        return off;
    }

    while (v > 0 && n < (int)sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n > 0) {
        buf[off++] = tmp[--n];
    }
    return off;
}

static void build_proc_status_path(char *buf, int pid) {
    int off = 0;
    static const char prefix[] = "/proc/";
    static const char suffix[] = "/status";

    for (int i = 0; i < (int)sizeof(prefix) - 1; i++) buf[off++] = prefix[i];
    off = append_uint(buf, off, (unsigned int)pid);
    for (int i = 0; i < (int)sizeof(suffix) - 1; i++) buf[off++] = suffix[i];
    buf[off] = 0;
}

static void build_pid_prefix(char *buf, int pid) {
    int off = 0;
    static const char prefix[] = "Pid:\t";

    for (int i = 0; i < (int)sizeof(prefix) - 1; i++) buf[off++] = prefix[i];
    off = append_uint(buf, off, (unsigned int)pid);
    buf[off] = 0;
}

static void build_self_link(char *buf, int pid) {
    int off = 0;
    static const char prefix[] = "/proc/";

    for (int i = 0; i < (int)sizeof(prefix) - 1; i++) buf[off++] = prefix[i];
    off = append_uint(buf, off, (unsigned int)pid);
    buf[off] = 0;
}

static int read_all(const char *path, char *buf, int cap) {
    int fd = open(path, 0);
    if (fd < 0) return fd;

    int total = 0;
    while (total < cap - 1) {
        int r = read(fd, buf + total, cap - 1 - total);
        if (r <= 0) break;
        total += r;
    }

    close(fd);
    buf[total] = 0;
    return total;
}

int main(void) {
    printf("=== proc_test ===\n");

    char buf[1024];

    int n = read_all("/proc/uptime", buf, sizeof(buf));
    check(n > 0, "/proc/uptime non-empty");
    check(contains(buf, "."), "/proc/uptime has '.'");

    n = read_all("/proc/meminfo", buf, sizeof(buf));
    check(n > 0, "/proc/meminfo non-empty");
    check(contains(buf, "MemFree:"), "/proc/meminfo has MemFree:");

    n = read_all("/proc/version", buf, sizeof(buf));
    check(n > 0, "/proc/version non-empty");
    check(contains(buf, "SBUnix"), "/proc/version has SBUnix");

    int mypid = getpid();
    char status_path[64];
    build_proc_status_path(status_path, mypid);

    n = read_all(status_path, buf, sizeof(buf));
    check(n > 0, "/proc/<self_pid>/status non-empty");

    char need[32];
    build_pid_prefix(need, mypid);
    check(contains(buf, need), "status has correct Pid:");

    char rbuf[64];
    long rl = readlink("/proc/self", rbuf, sizeof(rbuf) - 1);
    check(rl > 0, "readlink /proc/self");
    if (rl > 0) rbuf[rl] = 0;

    char want[32];
    build_self_link(want, mypid);
    check(strcmp(rbuf, want) == 0, "readlink /proc/self matches /proc/<pid>");

    int fd = open("/proc/999999/status", 0);
    check(fd == -ENOENT, "open /proc/999999/status -> ENOENT");

    long free_before = meminfo();
    check(free_before > 0, "meminfo before valid");

    for (int i = 0; i < 100; i++) {
        int f = open(status_path, 0);
        if (f >= 0) close(f);
    }

    long free_after = meminfo();
    check(free_after > 0, "meminfo after valid");
    check(free_before == free_after, "no procfs leak after 100 opens");

    printf("=== proc_test: %d failures ===\n", fails);
    return fails;
}
