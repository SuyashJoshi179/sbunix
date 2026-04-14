#pragma once

long write(int fd, const void *buf, long len);
int  getpid(void);
int  fork(void);
int  wait(int *status);
