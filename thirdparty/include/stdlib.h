#pragma once
/* Hide any non-POSIX itoa the student may declare with a 3-arg signature;
   thirdparty packages (busybox, etc.) declare their own internal 1-arg itoa. */
#define itoa __sbu_student_itoa
#include_next <stdlib.h>
#undef itoa

/* Fallback alloca for students whose headers don't provide one. */
#ifndef alloca
#define alloca(size) __builtin_alloca(size)
#endif

#define RAND_MAX 2147483647

long strtol(const char *s, char **endp, int base);
unsigned long strtoul(const char *s, char **endp, int base);
unsigned long long strtoull(const char *s, char **endp, int base);
long long strtoll(const char *s, char **endp, int base);
char *realpath(const char *path, char *resolved);
void srand(unsigned int seed);
int rand(void);
int mkstemp(char *tmpl);
void qsort(void *base, size_t nmemb, size_t size, int (*cmp)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*cmp)(const void *, const void *));
double strtod(const char *s, char **endp);
float strtof(const char *s, char **endp);
long double strtold(const char *s, char **endp);
int abs(int x);
long labs(long x);
long atol(const char *s);
int system(const char *cmd);
