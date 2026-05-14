#pragma once

#define POLLIN   0x001
#define POLLOUT  0x004
#define POLLERR  0x008
#define POLLHUP  0x010
#define POLLNVAL 0x020
#define POLLPRI  0x002

struct pollfd {
	int fd;
	short events;
	short revents;
};

typedef unsigned long nfds_t;
int poll(struct pollfd *fds, nfds_t nfds, int timeout);

#include <signal.h>
struct timespec;
int ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *tmo, const sigset_t *sigmask);
