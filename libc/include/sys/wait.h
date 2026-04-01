#pragma once

#include <sys/types.h>

#define WEXITSTATUS(status) ((status) & 0xff)
#define WIFEXITED(status)   (1)

pid_t waitpid(pid_t pid, int *wstatus, int options);
