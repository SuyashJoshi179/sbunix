#ifndef _SYS_SELECT_H
#define _SYS_SELECT_H

#include <sys/types.h>
#include <signal.h>
#include <time.h>

#define FD_SETSIZE 64

typedef struct {
    unsigned long fds_bits[(FD_SETSIZE + 8 * sizeof(long) - 1) / (8 * sizeof(long))];
} fd_set;

#define FD_ZERO(s) do { unsigned _i; \
    for (_i = 0; _i < sizeof((s)->fds_bits)/sizeof((s)->fds_bits[0]); _i++) \
        (s)->fds_bits[_i] = 0; \
} while (0)
#define FD_SET(d, s)   ((s)->fds_bits[(d)/(8*sizeof(long))] |=  (1UL << ((d) % (8*sizeof(long)))))
#define FD_CLR(d, s)   ((s)->fds_bits[(d)/(8*sizeof(long))] &= ~(1UL << ((d) % (8*sizeof(long)))))
#define FD_ISSET(d, s) (((s)->fds_bits[(d)/(8*sizeof(long))] &  (1UL << ((d) % (8*sizeof(long))))) != 0)

int select(int nfds, fd_set *r, fd_set *w, fd_set *e, struct timeval *tv);
int pselect(int nfds, fd_set *r, fd_set *w, fd_set *e,
            const struct timespec *tmo, const sigset_t *sigmask);

#endif
