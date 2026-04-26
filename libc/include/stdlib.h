#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#define RAND_MAX 0x7fffffff

void  exit(int status);
void  _Exit(int status);
void  abort(void);
int   atexit(void (*func)(void));

void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void  free(void *ptr);

int           atoi(const char *s);
long          atol(const char *s);
long long     atoll(const char *s);
long          strtol(const char *s, char **endp, int base);
unsigned long strtoul(const char *s, char **endp, int base);

int   abs(int x);
long  labs(long x);

int   rand(void);
void  srand(unsigned seed);

char *getenv(const char *name);

void  qsort(void *base, size_t nmemb, size_t size,
            int (*cmp)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*cmp)(const void *, const void *));

#endif
