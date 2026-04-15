#pragma once

long write(int fd, const void *buf, long len);
int  getpid(void);
int  getppid(void);
int  fork(void);
int  wait(int *status);
int  sched_yield(void);
int  sleep_ms(unsigned long ms);
int  usleep(unsigned long us);
