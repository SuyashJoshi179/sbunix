#pragma once

#include <sys/types.h>

void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
int atoi(const char *nptr);
void abort(void);
void exit(int);
