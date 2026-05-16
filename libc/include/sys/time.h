#pragma once
#include <time.h>

/* gettimeofday lives in time.h; this header is a compatibility shim.
 * utimes(2) lives here per POSIX. */
int utimes(const char *path, const struct timeval tv[2]);
