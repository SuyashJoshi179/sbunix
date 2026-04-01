#pragma once

long write(int fd, const void *buf, unsigned long count);
long exec(const char *path);
long open(const char *path);
long read(int fd, void *buf, unsigned long count);
long close(int fd);
long wait(void);
