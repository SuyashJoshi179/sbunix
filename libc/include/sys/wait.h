#pragma once
#include <sys/types.h>

/* POSIX wait-status word layout:
 *   bits 0-6  : termsig (0 if normal exit)
 *   bit  7    : core-dump flag (always 0 on SBUnix)
 *   bits 8-15 : exit code (only valid if termsig == 0)
 */

#define WNOHANG     1
#define WUNTRACED   2

#define WIFEXITED(s)    (((s) & 0x7f) == 0)
#define WEXITSTATUS(s)  (((s) >> 8) & 0xff)
#define WTERMSIG(s)     ((s) & 0x7f)
#define WIFSIGNALED(s)  (((s) & 0x7f) != 0 && ((s) & 0x7f) != 0x7f)
#define WCOREDUMP(s)    (((s) & 0x80) != 0)
#define WIFSTOPPED(s)   (0)
#define WSTOPSIG(s)     (0)

int wait(int *status);
int waitpid(int pid, int *status, int options);
