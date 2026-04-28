#pragma once
#include <sys/types.h>

/* SBUnix wait-status convention (documented in docs/SBUnix_Roadmap.md):
 * the kernel writes a single byte into the status int — either 0..127
 * for a normal exit code, or 128 + signum when the child was killed by
 * a signal. The macros below decode that directly. WCOREDUMP / WIFSTOPPED
 * / WSTOPSIG are stubbed (no core dumps, no job control). */

#define WNOHANG     1
#define WUNTRACED   2

#define WIFEXITED(s)    (((s) & 0x80) == 0)
#define WEXITSTATUS(s)  ((s) & 0x7f)
#define WIFSIGNALED(s)  (((s) & 0x80) != 0)
#define WTERMSIG(s)     ((s) & 0x7f)
#define WCOREDUMP(s)    (0)
#define WIFSTOPPED(s)   (0)
#define WSTOPSIG(s)     (0)

int wait(int *status);
int waitpid(int pid, int *status, int options);
