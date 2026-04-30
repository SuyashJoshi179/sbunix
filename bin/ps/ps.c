#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <ctype.h>
#include <string.h>

static int is_numeric_name(const char *s) {
    if (*s == '\0')
        return 0;
    for (; *s; s++) {
        if (!isdigit((unsigned char)*s))
            return 0;
    }
    return 1;
}

static int parse_pid(const char *s) {
    int pid = 0;
    if (!is_numeric_name(s))
        return -1;
    while (*s) {
        pid = pid * 10 + (*s - '0');
        s++;
    }
    return pid;
}

static int parse_pid_field(const char *s) {
    int pid = 0;
    int have_digit = 0;

    while (*s == ' ' || *s == '\t')
        s++;
    while (*s >= '0' && *s <= '9') {
        pid = pid * 10 + (*s - '0');
        have_digit = 1;
        s++;
    }

    return have_digit ? pid : -1;
}

static void copy_field_value(char *dst, int dst_sz, const char *src) {
    int i = 0;
    while (*src == ' ' || *src == '\t')
        src++;
    while (*src && *src != '\n' && i < dst_sz - 1) {
        dst[i++] = *src++;
    }
    dst[i] = '\0';
}

static int parse_status(const char *buf, char *name, int name_sz,
                        char *state, int state_sz, int *pid) {
    const char *p = buf;
    int have_name = 0, have_state = 0, have_pid = 0;

    while (*p) {
        const char *line = p;
        const char *nl = strchr(p, '\n');
        int len = nl ? (int)(nl - line) : (int)strlen(line);

        if (len >= 5 && strncmp(line, "Name:", 5) == 0) {
            copy_field_value(name, name_sz, line + 5);
            have_name = 1;
        } else if (len >= 6 && strncmp(line, "State:", 6) == 0) {
            copy_field_value(state, state_sz, line + 6);
            have_state = 1;
        } else if (len >= 4 && strncmp(line, "Pid:", 4) == 0) {
            int parsed = parse_pid_field(line + 4);
            if (parsed >= 0) {
                *pid = parsed;
                have_pid = 1;
            }
        }

        if (!nl)
            break;
        p = nl + 1;
    }

    return have_name && have_state && have_pid;
}

static int read_status_file(int pid, char *name, int name_sz,
                            char *state, int state_sz, int *out_pid) {
    char path[64];
    char buf[4096];
    int fd;
    long n;
    int total = 0;

    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;

    while ((n = read(fd, buf + total, (long)sizeof(buf) - 1 - total)) > 0) {
        total += (int)n;
        if (total >= (int)sizeof(buf) - 1)
            break;
    }
    close(fd);

    if (total <= 0)
        return 0;
    buf[total] = '\0';
    return parse_status(buf, name, name_sz, state, state_sz, out_pid);
}

int main(void) {
    int fd = open("/proc", O_RDONLY);
    if (fd < 0) {
        printf("ps: cannot open /proc\n");
        return 1;
    }

    printf("PID NAME STATE\n");

    char buf[2048];
    long n;
    while ((n = getdents64(fd, buf, sizeof(buf))) > 0) {
        long off = 0;
        while (off < n) {
            struct dirent64 *de = (struct dirent64 *)(buf + off);
            int pid;
            char name[128];
            char state[128];

            if (is_numeric_name(de->d_name)) {
                pid = parse_pid(de->d_name);
                if (pid >= 0 &&
                    read_status_file(pid, name, sizeof(name), state,
                                     sizeof(state), &pid)) {
                    printf("%d %s %s\n", pid, name, state);
                }
            }

            off += de->d_reclen;
        }
    }

    close(fd);
    return 0;
}
