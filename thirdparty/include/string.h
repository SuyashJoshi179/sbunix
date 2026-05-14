#pragma once
#include_next <string.h>

int strcasecmp(const char *a, const char *b);
int strncasecmp(const char *a, const char *b, size_t n);
char *strndup(const char *s, size_t n);
char *strpbrk(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);
char *strtok_r(char *s, const char *delim, char **saveptr);
char *strsep(char **sp, const char *delim);
char *stpcpy(char *dst, const char *src);
char *stpncpy(char *dst, const char *src, size_t n);
char *dirname(char *path);
char *strerror(int errnum);
size_t strspn(const char *s, const char *accept);
int strcoll(const char *a, const char *b);
