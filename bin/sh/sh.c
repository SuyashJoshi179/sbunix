#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>

#include "glob_match.h"

#define MAXLINE 256
#define MAXTOK  128
#define MAXARG  64

// Token types
enum { T_WORD, T_PIPE, T_REDIR_IN, T_REDIR_OUT, T_REDIR_APPEND, T_AND, T_BG, T_SEMI, T_END };

struct token {
    int  type;
    int  quoted;  // word came from a quoted literal — suppresses globbing
    char *val;
};

static char linebuf[MAXLINE];
static struct token tokens[MAXTOK];
static int ntokens;

// Arena for glob-expanded path strings; reset per command line.
static char glob_arena[8192];
static int  glob_arena_pos;

static int has_glob(const char *s) { return glob_has_meta(s); }

// Insertion-sort matched names into argv slots [start, end) lexicographically.
static void sort_range(char **argv, int start, int end) {
    for (int i = start + 1; i < end; i++) {
        char *cur = argv[i];
        int j = i;
        while (j > start && strcmp(argv[j - 1], cur) > 0) {
            argv[j] = argv[j - 1];
            j--;
        }
        argv[j] = cur;
    }
}

// Expand a single token into one or more tokens of `out`. Returns the
// number written. If no matches, falls back to writing the literal pattern.
static int expand_one(const char *pat, struct token *out, int cap) {
    if (cap <= 0) return 0;
    if (!has_glob(pat)) {
        out[0].type = T_WORD;
        out[0].val = (char *)pat;
        out[0].quoted = 0;
        return 1;
    }

    const char *slash = strrchr(pat, '/');
    char dirbuf[256];
    const char *dir;
    const char *base;
    size_t plen;
    if (slash) {
        size_t dlen = slash - pat;
        if (dlen == 0) { dirbuf[0] = '/'; dirbuf[1] = 0; }
        else {
            if (dlen > sizeof(dirbuf) - 1) dlen = sizeof(dirbuf) - 1;
            memcpy(dirbuf, pat, dlen);
            dirbuf[dlen] = 0;
        }
        dir = dirbuf;
        base = slash + 1;
        plen = (size_t)(slash - pat) + 1;
    } else {
        dir = ".";
        base = pat;
        plen = 0;
    }

    if (!has_glob(base)) {
        out[0].type = T_WORD;
        out[0].val = (char *)pat;
        out[0].quoted = 0;
        return 1;
    }

    int fd = open(dir, O_RDONLY);
    if (fd < 0) {
        out[0].type = T_WORD;
        out[0].val = (char *)pat;
        out[0].quoted = 0;
        return 1;
    }

    int n_out = 0;
    char buf[1024];
    char *names[MAXARG];
    int nnames = 0;
    long n;
    while ((n = getdents64(fd, buf, sizeof(buf))) > 0) {
        long off = 0;
        while (off < n) {
            struct dirent64 *de = (struct dirent64 *)(buf + off);
            off += de->d_reclen;
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
                continue;
            // Hidden files only match if pattern explicitly starts with '.'
            if (de->d_name[0] == '.' && base[0] != '.') continue;
            if (!glob_match(base, de->d_name)) continue;

            size_t nlen = strlen(de->d_name);
            // Build "<dir-prefix><name>" in the arena.
            if (glob_arena_pos + plen + nlen + 1 > sizeof(glob_arena)) {
                static int arena_warned;
                if (!arena_warned) {
                    const char *m = "sh: glob: too many matches, truncating\n";
                    write(2, m, strlen(m));
                    arena_warned = 1;
                }
                continue;
            }
            char *full = glob_arena + glob_arena_pos;
            if (plen) memcpy(full, pat, plen);
            memcpy(full + plen, de->d_name, nlen);
            full[plen + nlen] = 0;
            glob_arena_pos += plen + nlen + 1;

            if (nnames < MAXARG && n_out < cap) {
                names[nnames++] = full;
                n_out++;
            }
        }
    }
    close(fd);

    if (n_out == 0) {
        out[0].type = T_WORD;
        out[0].val = (char *)pat;
        out[0].quoted = 0;
        return 1;
    }

    sort_range(names, 0, nnames);
    for (int i = 0; i < nnames; i++) {
        out[i].type = T_WORD;
        out[i].val = names[i];
        out[i].quoted = 0;
    }
    return n_out;
}

static struct token expanded_tokens[MAXTOK];

static void expand_globs(void) {
    glob_arena_pos = 0;
    int nc = 0;
    for (int i = 0; i < ntokens && nc < MAXTOK - 1; i++) {
        if (tokens[i].type != T_WORD || tokens[i].quoted) {
            expanded_tokens[nc++] = tokens[i];
            continue;
        }
        int written = expand_one(tokens[i].val,
                                 &expanded_tokens[nc],
                                 MAXTOK - 1 - nc);
        nc += written;
    }
    expanded_tokens[nc].type = T_END;
    expanded_tokens[nc].val = 0;
    for (int i = 0; i <= nc; i++) tokens[i] = expanded_tokens[i];
    ntokens = nc;
}

static volatile int shell_interrupted;

static void on_sigint(int sig) {
    (void)sig;
    shell_interrupted = 1;
}

static volatile int sigchld_pending;
static void on_sigchld(int sig) {
    (void)sig;
    sigchld_pending = 1;
}

static void set_console_fg(int pgid) {
    ioctl(0, TIOCSPGRP, &pgid);
}

/* ---------------- Job table ---------------- */
#define MAX_JOBS 16
enum { JOB_FREE = 0, JOB_RUNNING, JOB_STOPPED, JOB_DONE };
struct job {
    int  state;
    int  pgid;
    int  id;          /* %n */
    int  status;      /* last reported status (for DONE) */
    char cmd[96];
};
static struct job jobs_tbl[MAX_JOBS];
static int next_job_id = 1;
static int shell_pgid;

static int jobs_alloc_slot(void) {
    for (int i = 0; i < MAX_JOBS; i++)
        if (jobs_tbl[i].state == JOB_FREE) return i;
    return -1;
}

static int jobs_find_by_id(int id) {
    for (int i = 0; i < MAX_JOBS; i++)
        if (jobs_tbl[i].state != JOB_FREE && jobs_tbl[i].id == id) return i;
    return -1;
}

static int jobs_most_recent(void) {
    int best = -1;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs_tbl[i].state == JOB_FREE) continue;
        if (best < 0 || jobs_tbl[i].id > jobs_tbl[best].id) best = i;
    }
    return best;
}

static const char *job_state_str(int s) {
    if (s == JOB_RUNNING) return "Running";
    if (s == JOB_STOPPED) return "Stopped";
    return "Done";
}

static void jobs_print_one(struct job *j) {
    printf("[%d] %d  %s\t%s\n", j->id, j->pgid, job_state_str(j->state), j->cmd);
}

/* Drain finished/stopped/continued children; print and update table.
 * Called between prompts. */
static void jobs_poll(int announce_done) {
    while (1) {
        int st = 0;
        int pid = wait4(-1, &st, 1 /*WNOHANG*/ | 2 /*WUNTRACED*/ | 8 /*WCONTINUED*/, 0);
        if (pid <= 0) return;
        int slot = -1;
        for (int i = 0; i < MAX_JOBS; i++) {
            if (jobs_tbl[i].state == JOB_FREE) continue;
            /* All pipeline members share pgid; the reaped pid is one of them.
             * We can't directly map pid→pgid here, so match if pid == pgid
             * (the leader) or if no exact match, match the most-recent. */
            if (jobs_tbl[i].pgid == pid) { slot = i; break; }
        }
        if (slot < 0) continue;
        struct job *j = &jobs_tbl[slot];
        if (WIFSTOPPED(st)) {
            j->state = JOB_STOPPED;
            j->status = st;
            printf("\n[%d]+ Stopped     %s\n", j->id, j->cmd);
        } else if (WIFCONTINUED(st)) {
            j->state = JOB_RUNNING;
        } else {
            j->state = JOB_DONE;
            j->status = st;
            if (announce_done) printf("[%d]+ Done        %s\n", j->id, j->cmd);
            j->state = JOB_FREE;
        }
    }
}

static int readline(void) {
    write(2, "sh> ", 4);
    long n = read(0, linebuf, MAXLINE - 1);
    if (n == -1 && errno == EINTR) {
        linebuf[0] = 0;
        write(2, "\n", 1);
        return 0;
    }
    if (n <= 0) return -1;
    /* If we filled the buffer without seeing a newline, the line is
     * longer than MAXLINE-1. Tell the user and drain the rest of the
     * line so the next prompt doesn't pick up its tail. */
    if (n == MAXLINE - 1 && linebuf[n - 1] != '\n') {
        const char *m = "sh: line too long, truncated\n";
        write(2, m, strlen(m));
        char drain[64];
        for (;;) {
            long d = read(0, drain, sizeof(drain));
            if (d <= 0) break;
            if (drain[d - 1] == '\n') break;
        }
    }
    if (n > 0 && linebuf[n - 1] == '\n') n--;
    linebuf[n] = 0;
    return (int)n;
}

static int is_space(char c) {
    return c == ' ' || c == '\t';
}

static void tokenize(void) {
    ntokens = 0;
    char *p = linebuf;
    while (*p && ntokens < MAXTOK - 1) {
        while (is_space(*p)) p++;
        if (!*p) break;

        tokens[ntokens].quoted = 0;
        tokens[ntokens].val = 0;

        if (*p == '|') {
            tokens[ntokens].type = T_PIPE;
            tokens[ntokens].val = 0;
            ntokens++; p++;
        } else if (*p == '&' && p[1] == '&') {
            tokens[ntokens].type = T_AND;
            tokens[ntokens].val = 0;
            ntokens++; p += 2;
        } else if (*p == '&') {
            tokens[ntokens].type = T_BG;
            tokens[ntokens].val = 0;
            ntokens++; p++;
        } else if (*p == ';') {
            /* POSIX statement separator — like && but continues regardless
             * of the previous command's exit status. */
            tokens[ntokens].type = T_SEMI;
            tokens[ntokens].val = 0;
            ntokens++; p++;
        } else if (*p == '<') {
            tokens[ntokens].type = T_REDIR_IN;
            tokens[ntokens].val = 0;
            ntokens++; p++;
        } else if (*p == '>') {
            p++;
            if (*p == '>') {
                tokens[ntokens].type = T_REDIR_APPEND;
                p++;
            } else {
                tokens[ntokens].type = T_REDIR_OUT;
            }
            tokens[ntokens].val = 0;
            ntokens++;
        } else if (*p == '"') {
            p++;
            tokens[ntokens].type = T_WORD;
            tokens[ntokens].quoted = 1;
            tokens[ntokens].val = p;
            while (*p && *p != '"') p++;
            if (*p == '"') *p++ = 0;
            ntokens++;
        } else if (*p == '\'') {
            /* Single-quoted literal: same shape as the double-quoted
             * branch — we don't expand $vars or escapes inside double
             * quotes either, so the two are functionally equivalent
             * for now. The flag is still useful for glob suppression. */
            p++;
            tokens[ntokens].type = T_WORD;
            tokens[ntokens].quoted = 1;
            tokens[ntokens].val = p;
            while (*p && *p != '\'') p++;
            if (*p == '\'') *p++ = 0;
            ntokens++;
        } else {
            tokens[ntokens].type = T_WORD;
            tokens[ntokens].quoted = 0;
            tokens[ntokens].val = p;
            while (*p && !is_space(*p) && *p != '|' && *p != '<' && *p != '>'
                   && *p != ';' && *p != '&') p++;
            if (*p) { *p = 0; p++; }
            ntokens++;
        }
    }
    /* Hitting the cap with non-whitespace remaining means we silently
     * dropped tokens. Warn so the user notices rather than getting
     * mysterious "missing argument" behaviour. */
    while (*p && is_space(*p)) p++;
    if (*p) {
        const char *m = "sh: too many tokens, command truncated\n";
        write(2, m, strlen(m));
    }
    tokens[ntokens].type = T_END;
}

// Parse a simple command segment (between pipes).
// Returns the token index after this command.
static int parse_cmd(int start, char **argv, int *argc_out,
                     char **redir_in, char **redir_out, int *append) {
    int ac = 0;
    *redir_in = 0;
    *redir_out = 0;
    *append = 0;

    int i = start;
    while (i < ntokens && tokens[i].type != T_PIPE && tokens[i].type != T_END &&
           tokens[i].type != T_BG && tokens[i].type != T_AND &&
           tokens[i].type != T_SEMI) {
        if (tokens[i].type == T_REDIR_IN) {
            i++;
            if (i < ntokens && tokens[i].type == T_WORD)
                *redir_in = tokens[i].val;
            i++;
        } else if (tokens[i].type == T_REDIR_OUT) {
            i++;
            if (i < ntokens && tokens[i].type == T_WORD)
                *redir_out = tokens[i].val;
            *append = 0;
            i++;
        } else if (tokens[i].type == T_REDIR_APPEND) {
            i++;
            if (i < ntokens && tokens[i].type == T_WORD)
                *redir_out = tokens[i].val;
            *append = 1;
            i++;
        } else if (tokens[i].type == T_WORD) {
            if (ac < MAXARG - 1)
                argv[ac++] = tokens[i].val;
            i++;
        } else {
            i++;
        }
    }
    argv[ac] = 0;
    *argc_out = ac;
    return i;
}

static char pathbuf[128];

static int parse_int(const char *s) {
    int sign = 1;
    int v = 0;
    if (*s == '-') {
        sign = -1;
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return sign * v;
}

static char *resolve_path(const char *cmd) {
    if (cmd[0] == '/' || cmd[0] == '.') return (char *)cmd;
    // Try /bin/cmd
    int ci = 0;
    const char *prefix = "/bin/";
    while (prefix[ci]) { pathbuf[ci] = prefix[ci]; ci++; }
    int j = 0;
    while (cmd[j] && ci < 126) { pathbuf[ci++] = cmd[j++]; }
    pathbuf[ci] = 0;
    return pathbuf;
}

static void run_simple(char **argv, int argc, char *redir_in, char *redir_out,
                        int append) {
    if (argc == 0) return;

    // Handle redirections — errors go to stderr (fd 2), not stdout.
    if (redir_in) {
        int fd = open(redir_in, O_RDONLY);
        if (fd < 0) {
            const char *msg = "sh: cannot open '";
            write(2, msg, 17);
            write(2, redir_in, strlen(redir_in));
            write(2, "'\n", 2);
            exit(1);
        }
        close(0);
        dup(fd);
        close(fd);
    }
    if (redir_out) {
        int flags = O_WRONLY | O_CREAT;
        if (append) flags |= O_APPEND;
        else flags |= O_TRUNC;
        int fd = open(redir_out, flags);
        if (fd < 0) {
            const char *msg = "sh: cannot open '";
            write(2, msg, 17);
            write(2, redir_out, strlen(redir_out));
            write(2, "'\n", 2);
            exit(1);
        }
        close(1);
        dup(fd);
        close(fd);
    }

    char *path = resolve_path(argv[0]);
    execv(path, argv);
    /* POSIX: exec failure is reported to stderr, not stdout, and the
     * exit code distinguishes "not found" (127) from other failures
     * such as ELF errors or EACCES (126). */
    int err = errno;
    const char *msg = "sh: exec '";
    write(2, msg, 11);
    write(2, argv[0], strlen(argv[0]));
    write(2, "' failed\n", 9);
    exit(err == ENOENT ? 127 : 126);
}

// Execute the pipeline starting from token index `start`.
/* Reset job-control signals to default before exec (children inherit
 * the shell's SIG_IGN otherwise). */
static void child_reset_signals(void) {
    struct sigaction sa = {0};
    sa.sa_handler = SIG_DFL;
    sigaction(SIGINT,  &sa, 0);
    sigaction(SIGQUIT, &sa, 0);
    sigaction(SIGTSTP, &sa, 0);
    sigaction(SIGTTIN, &sa, 0);
    sigaction(SIGTTOU, &sa, 0);
}

/* Recursive pipeline launcher. Tracks the leader pgid via *leader_io.
 * Returns the pid of the child it forked (caller uses this for the
 * last stage's status). Children: setpgid(0, leader). Parent: also
 * setpgid(child, leader) for race coverage. */
static int spawn_pipeline(int start, int in_fd, int *leader_io,
                          int *last_pid_out, char *first_cmd_buf,
                          int first_cmd_buf_sz) {
    char *argv[MAXARG];
    int argc;
    char *redir_in, *redir_out;
    int append;

    int next = parse_cmd(start, argv, &argc, &redir_in, &redir_out, &append);
    int has_pipe = (next < ntokens && tokens[next].type == T_PIPE);

    /* Capture command label on first stage for the job table. */
    if (first_cmd_buf && first_cmd_buf_sz > 0 && first_cmd_buf[0] == 0) {
        int pos = 0;
        for (int i = 0; i < argc && pos < first_cmd_buf_sz - 1; i++) {
            if (i && pos < first_cmd_buf_sz - 1) first_cmd_buf[pos++] = ' ';
            const char *s = argv[i];
            while (*s && pos < first_cmd_buf_sz - 1) first_cmd_buf[pos++] = *s++;
        }
        first_cmd_buf[pos] = 0;
    }

    if (has_pipe) {
        int pfd[2];
        if (pipe(pfd) < 0) { printf("sh: pipe failed\n"); return 1; }
        int pid = fork();
        if (pid < 0) {
            close(pfd[0]); close(pfd[1]);
            if (in_fd >= 0) close(in_fd);
            printf("sh: fork failed\n");
            return 1;
        }
        if (pid == 0) {
            int leader = (*leader_io == 0) ? 0 /* self */ : *leader_io;
            setpgid(0, leader);
            child_reset_signals();
            close(pfd[0]);
            if (in_fd >= 0) { close(0); dup(in_fd); close(in_fd); }
            close(1); dup(pfd[1]); close(pfd[1]);
            run_simple(argv, argc, redir_in, 0, 0);
        }
        if (*leader_io == 0) *leader_io = pid;
        setpgid(pid, *leader_io);

        close(pfd[1]);
        if (in_fd >= 0) close(in_fd);

        return spawn_pipeline(next + 1, pfd[0], leader_io, last_pid_out,
                              first_cmd_buf, first_cmd_buf_sz);
    } else {
        int pid = fork();
        if (pid < 0) {
            if (in_fd >= 0) close(in_fd);
            printf("sh: fork failed\n");
            return 1;
        }
        if (pid == 0) {
            int leader = (*leader_io == 0) ? 0 : *leader_io;
            setpgid(0, leader);
            child_reset_signals();
            if (in_fd >= 0) { close(0); dup(in_fd); close(in_fd); }
            run_simple(argv, argc, redir_in, redir_out, append);
        }
        if (*leader_io == 0) *leader_io = pid;
        setpgid(pid, *leader_io);
        if (in_fd >= 0) close(in_fd);
        if (last_pid_out) *last_pid_out = pid;
        return 0;
    }
}

/* Wait foreground pipeline: wait for every member of pgrp `pgid` to
 * exit or for any to stop. Returns last exit status. If a child
 * stops, mark the existing job slot stopped (slot >= 0) or allocate
 * a fresh one (slot == -1). */
static int wait_fg_pgrp(int pgid, int last_pid, char *cmdbuf, int reuse_slot) {
    int last_status = 0;
    int stopped = 0;
    while (1) {
        int st = 0;
        int r = wait4(-pgid, &st, 2 /*WUNTRACED*/, 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (WIFSTOPPED(st)) {
            stopped = 1;
            last_status = st;
            /* Whole pgrp shares the stop signal — break and report once. */
            break;
        }
        if (r == last_pid) last_status = st;
    }

    set_console_fg(shell_pgid);

    if (stopped) {
        int slot = reuse_slot;
        if (slot < 0) {
            slot = jobs_alloc_slot();
            if (slot < 0) return last_status;
            jobs_tbl[slot].pgid = pgid;
            jobs_tbl[slot].id = next_job_id++;
            int n = 0;
            while (cmdbuf && cmdbuf[n] && n < (int)sizeof(jobs_tbl[slot].cmd) - 1) {
                jobs_tbl[slot].cmd[n] = cmdbuf[n]; n++;
            }
            jobs_tbl[slot].cmd[n] = 0;
        }
        jobs_tbl[slot].state = JOB_STOPPED;
        jobs_tbl[slot].status = last_status;
        printf("\n[%d]+ Stopped\t%s\n", jobs_tbl[slot].id, jobs_tbl[slot].cmd);
    }
    return last_status;
}

/* Run a single pipeline. `bg` = trailing & detected. Returns status. */
static int run_pipeline(int start, int in_fd, int bg) {
    int leader = 0;
    int last_pid = 0;
    char cmdbuf[96] = {0};
    spawn_pipeline(start, in_fd, &leader, &last_pid, cmdbuf, sizeof(cmdbuf));
    if (leader == 0) return 0;

    if (bg) {
        int slot = jobs_alloc_slot();
        if (slot < 0) {
            printf("sh: too many background jobs\n");
            return 0;
        }
        jobs_tbl[slot].state = JOB_RUNNING;
        jobs_tbl[slot].pgid = leader;
        jobs_tbl[slot].id = next_job_id++;
        int n = 0;
        while (cmdbuf[n] && n < (int)sizeof(jobs_tbl[slot].cmd) - 1) {
            jobs_tbl[slot].cmd[n] = cmdbuf[n]; n++;
        }
        jobs_tbl[slot].cmd[n] = 0;
        printf("[%d] %d\n", jobs_tbl[slot].id, leader);
        return 0;
    }

    set_console_fg(leader);
    return wait_fg_pgrp(leader, last_pid, cmdbuf, -1);
}

// Run the already-tokenized line. Returns the exit status of the last
// external command (0..255), or 0 for built-ins that succeed. The `exit`
// built-in does not return — it terminates the shell directly.
static int run_line(void) {
    if (ntokens == 0) return 0;

    if (tokens[0].type == T_WORD && strcmp(tokens[0].val, "exit") == 0) {
        int code = 0;
        if (ntokens > 1 && tokens[1].type == T_WORD)
            code = parse_int(tokens[1].val);
        _exit(code & 0xff);
    }

    /* exec — replace the current shell with the named binary (POSIX
     * special builtin). `exec` alone is treated as a no-op (the
     * redirection-only form is not implemented). If any operator follows
     * the command word — pipe, &&, ;, &, <, >, >> — fall through to the
     * normal pipeline path so fork-based semantics still apply. */
    if (tokens[0].type == T_WORD && strcmp(tokens[0].val, "exec") == 0) {
        if (ntokens == 1) return 0;
        int has_op = 0;
        for (int i = 1; i < ntokens; i++) {
            if (tokens[i].type != T_WORD && tokens[i].type != T_END) {
                has_op = 1;
                break;
            }
        }
        if (!has_op) {
            char *eargv[MAXARG + 1];
            int eargc = 0;
            for (int i = 1; i < ntokens && eargc < MAXARG; i++) {
                if (tokens[i].type == T_WORD)
                    eargv[eargc++] = tokens[i].val;
            }
            eargv[eargc] = 0;
            char *path = resolve_path(eargv[0]);
            execv(path, eargv);
            /* execv only returns on failure. POSIX: exec failure in a
             * non-interactive shell is fatal (script-shell exits 127). */
            const char *m1 = "sh: exec '";
            write(2, m1, 10);
            write(2, eargv[0], strlen(eargv[0]));
            write(2, "': ", 3);
            const char *es = strerror(errno);
            write(2, es, strlen(es));
            write(2, "\n", 1);
            _exit(127);
        }
    }

    if (tokens[0].type == T_WORD && strcmp(tokens[0].val, "cd") == 0) {
        char *dir = (ntokens > 1 && tokens[1].type == T_WORD) ? tokens[1].val : "/";
        if (chdir(dir) < 0) {
            printf("cd: '%s': no such directory\n", dir);
            return 1;
        }
        return 0;
    }

    if (tokens[0].type == T_WORD && strcmp(tokens[0].val, "pwd") == 0) {
        char cwdbuf[256];
        if (getcwd(cwdbuf, sizeof(cwdbuf)) >= 0)
            printf("%s\n", cwdbuf);
        return 0;
    }

    if (tokens[0].type == T_WORD && strcmp(tokens[0].val, "kill") == 0) {
        int sig = SIGTERM;
        int argi = 1;
        if (ntokens > 2 && tokens[1].type == T_WORD && tokens[1].val[0] == '-') {
            sig = parse_int(tokens[1].val + 1);
            argi = 2;
        }
        if (argi >= ntokens || tokens[argi].type != T_WORD) {
            printf("usage: kill [-sig] <pid>\n");
            return 1;
        }
        int pid = parse_int(tokens[argi].val);
        int rc = kill(pid, sig);
        if (rc < 0) {
            printf("kill: failed (%d)\n", rc);
            return 1;
        }
        return 0;
    }

    if (tokens[0].type == T_WORD && strcmp(tokens[0].val, "echo") == 0) {
        int has_redir = 0;
        for (int i = 1; i < ntokens; i++) {
            if (tokens[i].type == T_REDIR_OUT || tokens[i].type == T_REDIR_APPEND ||
                tokens[i].type == T_PIPE) {
                has_redir = 1;
                break;
            }
        }
        if (!has_redir) {
            for (int i = 1; i < ntokens; i++) {
                if (tokens[i].type == T_WORD) {
                    if (i > 1) write(1, " ", 1);
                    write(1, tokens[i].val, strlen(tokens[i].val));
                }
            }
            write(1, "\n", 1);
            return 0;
        }
    }

    /* Builtins for job control. */
    if (tokens[0].type == T_WORD && strcmp(tokens[0].val, "jobs") == 0) {
        for (int i = 0; i < MAX_JOBS; i++)
            if (jobs_tbl[i].state != JOB_FREE) jobs_print_one(&jobs_tbl[i]);
        return 0;
    }
    if (tokens[0].type == T_WORD &&
        (strcmp(tokens[0].val, "fg") == 0 || strcmp(tokens[0].val, "bg") == 0)) {
        int is_fg = (tokens[0].val[0] == 'f');
        int slot;
        if (ntokens > 1 && tokens[1].type == T_WORD && tokens[1].val[0] == '%')
            slot = jobs_find_by_id(parse_int(tokens[1].val + 1));
        else
            slot = jobs_most_recent();
        if (slot < 0) { printf("%s: no such job\n", is_fg ? "fg" : "bg"); return 1; }
        struct job *j = &jobs_tbl[slot];
        int was_stopped = (j->state == JOB_STOPPED);
        if (is_fg) set_console_fg(j->pgid);
        if (was_stopped) kill(-j->pgid, SIGCONT);
        j->state = JOB_RUNNING;
        if (is_fg) {
            printf("%s\n", j->cmd);
            int st = wait_fg_pgrp(j->pgid, 0, j->cmd, slot);
            if (!WIFSTOPPED(st)) j->state = JOB_FREE;
            return WEXITSTATUS(st);
        } else {
            printf("[%d] %s &\n", j->id, j->cmd);
            return 0;
        }
    }

    int cmd_start = 0;
    int last_status = 0;
    for (int i = 0;; i++) {
        if (tokens[i].type == T_AND || tokens[i].type == T_END ||
            tokens[i].type == T_BG  || tokens[i].type == T_SEMI) {
            int saved = tokens[i].type;
            int bg = (saved == T_BG);
            tokens[i].type = T_END;
            last_status = run_pipeline(cmd_start, -1, bg);
            tokens[i].type = saved;

            if (saved == T_END) break;
            if (saved == T_AND && last_status != 0) break;
            cmd_start = i + 1;
            if (cmd_start >= ntokens) break;
        }
    }
    return WEXITSTATUS(last_status);
}

/* Substitute `$0` in `src` with `arg0`, writing into `dst` (capacity
 * `cap`, including NUL). Returns 0 on success, -1 if the result would
 * not fit. Only `$0` is recognised (no `$1`..`$9`, no `${var}`); that
 * matches what /etc/rc actually uses today. */
static int subst_dollar0(char *dst, size_t cap, const char *src,
                         const char *arg0) {
    size_t alen = strlen(arg0);
    size_t di = 0;
    for (size_t si = 0; src[si]; si++) {
        if (src[si] == '$' && src[si + 1] == '0') {
            if (di + alen >= cap) return -1;
            memcpy(dst + di, arg0, alen);
            di += alen;
            si++;  /* skip the '0' */
            continue;
        }
        if (di + 1 >= cap) return -1;
        dst[di++] = src[si];
    }
    dst[di] = 0;
    return 0;
}

/* Run a /etc/rc-style script: read once, iterate lines, tokenize and
 * run each. Comments (# ...) and shebang lines are skipped. `exec` is
 * handled by run_line as a POSIX-special builtin, so a trailing
 * `exec /bin/sh` replaces the script-shell with an interactive shell.
 * `$0` in script lines expands to the script path so that the
 * conventional `echo Running $0` produces the expected output. */
static int run_script(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("sh: cannot open '%s'\n", path);
        return 1;
    }
    static char buf[4096];
    long n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n < 0) return 1;
    buf[n] = 0;

    int last = 0;
    char *p = buf;
    while (*p) {
        char *eol = p;
        while (*eol && *eol != '\n') eol++;
        char saved = *eol;
        *eol = 0;
        char *q = p;
        while (*q == ' ' || *q == '\t') q++;
        if (*q && *q != '#') {
            if (subst_dollar0(linebuf, sizeof(linebuf), q, path) < 0) {
                size_t len = strlen(q);
                if (len >= sizeof(linebuf)) len = sizeof(linebuf) - 1;
                memcpy(linebuf, q, len);
                linebuf[len] = 0;
            }
            tokenize();
            expand_globs();
            last = run_line();
        }
        if (!saved) break;
        p = eol + 1;
    }
    return last;
}

int main(int argc, char **argv) {
    if (argc == 2 && argv[1][0] != '-') {
        return run_script(argv[1]) & 0xff;
    }
    if (argc >= 3 && strcmp(argv[1], "-c") == 0) {
        size_t n = strlen(argv[2]);
        if (n >= sizeof(linebuf)) n = sizeof(linebuf) - 1;
        memcpy(linebuf, argv[2], n);
        linebuf[n] = 0;
        tokenize();
        expand_globs();
        return run_line() & 0xff;
    }

    /* Become our own process group leader and claim the controlling tty. */
    setpgid(0, 0);
    shell_pgid = getpgrp();
    set_console_fg(shell_pgid);

    /* Ignore job-control signals so the shell doesn't kill or stop itself
     * when reclaiming the tty or when the user types ^C/^Z at the prompt.
     * Children reset these to SIG_DFL before exec via child_reset_signals. */
    struct sigaction sa = {0};
    sa.sa_handler = SIG_IGN;
    sigaction(SIGTTIN, &sa, 0);
    sigaction(SIGTTOU, &sa, 0);
    sigaction(SIGTSTP, &sa, 0);
    sigaction(SIGQUIT, &sa, 0);
    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, 0);

    /* SIGCHLD handler: just sets a flag. Its presence makes the kernel
     * treat SIGCHLD as actionable, so the read(0, ...) in readline()
     * returns -EINTR when a background job changes state. The main loop
     * then re-runs jobs_poll(1) and we get the Done/Stopped line printed
     * before the next prompt instead of after the next command. */
    sa.sa_handler = on_sigchld;
    sigaction(SIGCHLD, &sa, 0);

    while (1) {
        jobs_poll(1);

        int rl = readline();
        if (rl < 0) break;
        if (linebuf[0] == 0) continue;

        if (shell_interrupted) {
            shell_interrupted = 0;
        }

        tokenize();
        expand_globs();
        run_line();
    }

    return 0;
}
