#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

#define MAXLINE 256
#define MAXTOK  32
#define MAXARG  16

// Token types
enum { T_WORD, T_PIPE, T_REDIR_IN, T_REDIR_OUT, T_REDIR_APPEND, T_END };

struct token {
    int  type;
    char *val;
};

static char linebuf[MAXLINE];
static struct token tokens[MAXTOK];
static int ntokens;

static int readline(void) {
    write(2, "sh> ", 4);
    long n = read(0, linebuf, MAXLINE - 1);
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
static void run_pipeline(int start, int in_fd) {
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
            return;
        }

        int pid = fork();
        if (pid == 0) {
            close(pfd[0]);
            if (in_fd >= 0) { close(0); dup(in_fd); close(in_fd); }
            close(1);
            dup(pfd[1]);
            close(pfd[1]);
            run_simple(argv, argc, redir_in, 0, 0);
        }

        close(pfd[1]);
        if (in_fd >= 0) close(in_fd);

        run_pipeline(next + 1, pfd[0]);
        wait(0);
    } else {
        int pid = fork();
        if (pid == 0) {
            if (in_fd >= 0) { close(0); dup(in_fd); close(in_fd); }
            run_simple(argv, argc, redir_in, redir_out, append);
        }
        if (in_fd >= 0) close(in_fd);
        wait(0);
    }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    while (1) {
        if (readline() < 0) break;
        if (linebuf[0] == 0) continue;

        tokenize();
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

        run_pipeline(0, -1);
    }

    return 0;
}
