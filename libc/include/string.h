#pragma once
#include <stdint.h>
#include <stddef.h>

size_t  strlen(const char *s);
size_t  strnlen(const char *s, size_t n);
int     strcmp(const char *a, const char *b);
int     strncmp(const char *a, const char *b, size_t n);
int     strcoll(const char *a, const char *b);
int     strerror_r(int errnum, char *buf, size_t buflen);
size_t  strxfrm(char *dst, const char *src, size_t n);
size_t  strlcpy(char *dst, const char *src, size_t n);
size_t  strlcat(char *dst, const char *src, size_t n);
void   *memccpy(void *dst, const void *src, int c, size_t n);
void   *memmem(const void *hay, size_t hlen, const void *needle, size_t nlen);
char   *strcpy(char *dst, const char *src);
char   *strncpy(char *dst, const char *src, size_t n);
char   *strcat(char *dst, const char *src);
char   *strncat(char *dst, const char *src, size_t n);
char   *strchr(const char *s, int c);
char   *strrchr(const char *s, int c);
char   *strstr(const char *hay, const char *needle);
char   *strpbrk(const char *s, const char *accept);
size_t  strspn(const char *s, const char *accept);
size_t  strcspn(const char *s, const char *reject);
char   *strtok(char *s, const char *delim);
char   *strtok_r(char *s, const char *delim, char **saveptr);
char   *strdup(const char *s);
char   *strndup(const char *s, size_t n);
char   *strerror(int errnum);
char   *strsignal(int sig);

void   *memset(void *dst, int c, size_t n);
void   *memcpy(void *dst, const void *src, size_t n);
void   *memmove(void *dst, const void *src, size_t n);
int     memcmp(const void *a, const void *b, size_t n);
void   *memchr(const void *s, int c, size_t n);
void   *memrchr(const void *s, int c, size_t n);
void   *mempcpy(void *dst, const void *src, size_t n);

char   *strsep(char **stringp, const char *delim);
char   *strchrnul(const char *s, int c);
char   *stpcpy(char *dst, const char *src);
char   *stpncpy(char *dst, const char *src, size_t n);
char   *strcasestr(const char *hay, const char *needle);
int     strverscmp(const char *a, const char *b);
