#pragma once
#include <sys/types.h>

typedef struct {
	size_t gl_pathc;
	char **gl_pathv;
	size_t gl_offs;
} glob_t;

#define GLOB_NOSORT  (1 << 0)
#define GLOB_NOMATCH 3

int glob(const char *pattern, int flags,
         int (*errfunc)(const char *, int), glob_t *pglob);
void globfree(glob_t *pglob);
