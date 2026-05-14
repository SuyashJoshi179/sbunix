#pragma once
#include_next <sys/time.h>

#ifndef HAVE_SETTIMEOFDAY
int settimeofday(const struct timeval *tv, const struct timezone *tz);
#endif
int utimes(const char *path, const struct timeval times[2]);
