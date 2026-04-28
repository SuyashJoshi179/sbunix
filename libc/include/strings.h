#pragma once
#include <stddef.h>

void  bzero(void *s, size_t n);
int   bcmp(const void *a, const void *b, size_t n);
void  bcopy(const void *src, void *dst, size_t n);

int   ffs(int x);
int   ffsl(long x);
int   ffsll(long long x);

int   strcasecmp(const char *a, const char *b);
int   strncasecmp(const char *a, const char *b, size_t n);
