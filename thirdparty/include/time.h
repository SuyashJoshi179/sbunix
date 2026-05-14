#pragma once
#include_next <time.h>

struct tm *localtime(const time_t *t);
struct tm *gmtime_r(const time_t *t, struct tm *result);
size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm);
struct tm *localtime_r(const time_t *t, struct tm *result);
time_t mktime(struct tm *tm);
int clock_gettime(int clk_id, struct timespec *tp);
