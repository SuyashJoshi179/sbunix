#pragma once
#include <sys/types.h>

/* POSIX wait-status word layout:
 *   exited:    bits 0-6  = 0,    bits 8-15 = exit code
 *   signaled:  bits 0-6  = sig,  bit 7 = core flag (unused)
 *   stopped:   bits 0-7  = 0x7f, bits 8-15 = stop signal
 *   continued: 0xffff
 */

#define WNOHANG     1
#define WUNTRACED   2
#define WCONTINUED  8

#define WIFEXITED(s)    (((s) & 0x7f) == 0)
#define WEXITSTATUS(s)  (((s) >> 8) & 0xff)
#define WTERMSIG(s)     ((s) & 0x7f)
#define WIFSIGNALED(s)  (((s) & 0x7f) != 0 && ((s) & 0x7f) != 0x7f && ((s) & 0xff) != 0xff)
#define WCOREDUMP(s)    (((s) & 0x80) != 0)
#define WIFSTOPPED(s)   (((s) & 0xff) == 0x7f)
#define WSTOPSIG(s)     (((s) >> 8) & 0xff)
#define WIFCONTINUED(s) ((s) == 0xffff)

int wait(int *status);
int waitpid(int pid, int *status, int options);
int wait4(int pid, int *status, int options, void *rusage);
