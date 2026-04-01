#pragma once

#include <sys/types.h>

ssize_t write(int fd, const void *buf, unsigned long count);
int exec(const char *path);
int open(const char *path);
ssize_t read(int fd, void *buf, unsigned long count);
int close(int fd);
int wait(void);
int spawn(const char *path);
int getpid(void);
int kill(int pid, int sig);
