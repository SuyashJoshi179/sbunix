#pragma once

#include <sys/types.h>

#define SIGTERM 15
#define SIGKILL 9
#define SIGINT  2

int kill(pid_t pid, int sig);
