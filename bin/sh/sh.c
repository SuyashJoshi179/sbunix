#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>

#define MAXLINE 256
#define MAXTOK  128
#define MAXARG  64

// Token types
enum { T_WORD, T_PIPE, T_REDIR_IN, T_REDIR_OUT, T_REDIR_APPEND, T_AND, T_END };

struct token {
    int  type;
    char *val;
};

static char linebuf[MAXLINE];
static struct token tokens[MAXTOK];
static int ntokens;

// Arena for glob-expanded path strings; reset per command line.
static char glob_arena[8192];
static int  glob_arena_pos;

static int has_glob(const char *s) {
    while (*s) { if (*s == '*') return 1; s++; }
    return 0;
}

// Match a glob pattern containing '*' against name. '*' matches any
// sequence of characters (including empty). No '/' handling here —
// caller splits the pattern on the last '/'.
static int glob_match(const char *pat, const char *name) {
    if (*pat == 0) return *name == 0;
    if (*pat == '*') {
        while (*pat == '*') pat++;
        if (*pat == 0) return 1;
        while (*name) {
            if (glob_match(pat, name)) return 1;
            name++;
        }
        return 0;
    }
    if (*name == 0) return 0;
    if (*pat != *name) return 0;
    return glob_match(pat + 1, name + 1);
}

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
        return 1;
    }

    int fd = open(dir, O_RDONLY);
    if (fd < 0) {
        out[0].type = T_WORD;
        out[0].val = (char *)pat;
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
            if (glob_arena_pos + plen + nlen + 1 > sizeof(glob_arena)) continue;
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
        return 1;
    }

    sort_range(names, 0, nnames);
    for (int i = 0; i < nnames; i++) {
        out[i].type = T_WORD;
        out[i].val = names[i];
    }
    return n_out;
}

static struct token expanded_tokens[MAXTOK];

static void expand_globs(void) {
    glob_arena_pos = 0;
    int nc = 0;
    for (int i = 0; i < ntokens && nc < MAXTOK - 1; i++) {
        if (tokens[i].type != T_WORD) {
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

static void set_console_fg(int pid) {
    ioctl(0, TIOCSPGRP, &pid);
}

static int readline(void) {
    write(2, "sh> ", 4);
    long n = read(0, linebuf, MAXLINE - 1);
    if (n == -EINTR) {
        linebuf[0] = 0;
        write(2, "\n", 1);
        return 0;
    }
    if (n <= 0) return -1;
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

        if (*p == '|') {
            tokens[ntokens].type = T_PIPE;
            tokens[ntokens].val = 0;
            ntokens++; p++;
        } else if (*p == '&' && p[1] == '&') {
            tokens[ntokens].type = T_AND;
            tokens[ntokens].val = 0;
            ntokens++; p += 2;
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
            tokens[ntokens].val = p;
            while (*p && *p != '"') p++;
            if (*p == '"') *p++ = 0;
            ntokens++;
        } else {
            tokens[ntokens].type = T_WORD;
            tokens[ntokens].val = p;
            while (*p && !is_space(*p) && *p != '|' && *p != '<' && *p != '>') p++;
            if (*p) { *p = 0; p++; }
            ntokens++;
        }
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
    while (i < ntokens && tokens[i].type != T_PIPE && tokens[i].type != T_END) {
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

    // Handle redirections
    if (redir_in) {
        int fd = open(redir_in, O_RDONLY);
        if (fd < 0) {
            printf("sh: cannot open '%s'\n", redir_in);
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
            printf("sh: cannot open '%s'\n", redir_out);
            exit(1);
        }
        close(1);
        dup(fd);
        close(fd);
    }

    char *path = resolve_path(argv[0]);
    execv(path, argv);
    printf("sh: exec '%s' failed\n", argv[0]);
    exit(1);
}

// Execute the pipeline starting from token index `start`.
// If `in_fd` >= 0, stdin has been redirected to that fd.
static int run_pipeline(int start, int in_fd) {
    char *argv[MAXARG];
    int argc;
    char *redir_in, *redir_out;
    int append;

    int next = parse_cmd(start, argv, &argc, &redir_in, &redir_out, &append);

    int has_pipe = (next < ntokens && tokens[next].type == T_PIPE);

    if (has_pipe) {
        int pfd[2];
        if (pipe(pfd) < 0) {
            printf("sh: pipe failed\n");
            return 1;
        }

        int pid = fork();
        if (pid < 0) {
            close(pfd[0]);
            close(pfd[1]);
            if (in_fd >= 0) close(in_fd);
            printf("sh: fork failed\n");
            return 1;
        }
        if (pid == 0) {
            close(pfd[0]);
            if (in_fd >= 0) { close(0); dup(in_fd); close(in_fd); }
            close(1);
            dup(pfd[1]);
            close(pfd[1]);
            run_simple(argv, argc, redir_in, 0, 0);
        }

        set_console_fg(pid);

        close(pfd[1]);
        if (in_fd >= 0) close(in_fd);

        int st = run_pipeline(next + 1, pfd[0]);
        wait(0);
        return st;
    } else {
        int pid = fork();
        if (pid < 0) {
            if (in_fd >= 0) close(in_fd);
            printf("sh: fork failed\n");
            return 1;
        }
        if (pid == 0) {
            if (in_fd >= 0) { close(0); dup(in_fd); close(in_fd); }
            run_simple(argv, argc, redir_in, redir_out, append);
        }
        set_console_fg(pid);
        if (in_fd >= 0) close(in_fd);
        int st = 0;
        for (;;) {
            int r = wait(&st);
            if (r >= 0) break;
        }
        return st;
    }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    struct sigaction sa;
    sa.sa_handler = on_sigint;
    sa.sa_mask = 0;
    sa.sa_flags = 0;
    sa.sa_restorer = 0;
    sigaction(SIGINT, &sa, 0);

    set_console_fg(getpid());

    while (1) {
        int rl = readline();
        if (rl < 0) break;
        if (linebuf[0] == 0) continue;

        if (shell_interrupted) {
            shell_interrupted = 0;
        }

        tokenize();
        expand_globs();
        if (ntokens == 0) continue;

        // Builtins
        if (tokens[0].type == T_WORD && strcmp(tokens[0].val, "exit") == 0)
            break;

        if (tokens[0].type == T_WORD && strcmp(tokens[0].val, "cd") == 0) {
            char *dir = (ntokens > 1 && tokens[1].type == T_WORD) ? tokens[1].val : "/";
            if (chdir(dir) < 0)
                printf("cd: '%s': no such directory\n", dir);
            continue;
        }

        if (tokens[0].type == T_WORD && strcmp(tokens[0].val, "pwd") == 0) {
            char cwdbuf[256];
            if (getcwd(cwdbuf, sizeof(cwdbuf)) >= 0)
                printf("%s\n", cwdbuf);
            continue;
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
                continue;
            }
            int pid = parse_int(tokens[argi].val);
            int rc = kill(pid, sig);
            if (rc < 0)
                printf("kill: failed (%d)\n", rc);
            continue;
        }

        // echo builtin for simplicity
        if (tokens[0].type == T_WORD && strcmp(tokens[0].val, "echo") == 0) {
            // Check for redirection — if present, fork+exec instead
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
                continue;
            }
        }

        int cmd_start = 0;
        int last_status = 0;

        for (int i = 0;; i++) {
            if (tokens[i].type == T_AND || tokens[i].type == T_END) {
                int saved = tokens[i].type;
                tokens[i].type = T_END;
                last_status = run_pipeline(cmd_start, -1);
                tokens[i].type = saved;
                set_console_fg(getpid());

                if (saved == T_END)
                    break;
                if (last_status != 0)
                    break;
                cmd_start = i + 1;
            }
        }
    }

    return 0;
}
