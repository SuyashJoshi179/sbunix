#pragma once
#include <sys/types.h>
#include <stdarg.h>

typedef struct _FILE FILE;
extern FILE *stdin, *stdout, *stderr;

#define EOF (-1)
#define BUFSIZ 1024
#define FILENAME_MAX 256
#define FOPEN_MAX 16

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2

int printf(const char *, ...);
int fprintf(FILE *, const char *, ...);
int sprintf(char *, const char *, ...);
int snprintf(char *, size_t, const char *, ...);
int dprintf(int, const char *, ...);
int vprintf(const char *, va_list);
int vfprintf(FILE *, const char *, va_list);
int vsnprintf(char *, size_t, const char *, va_list);
int sscanf(const char *, const char *, ...);
FILE *fopen(const char *, const char *);
FILE *fdopen(int, const char *);
int fclose(FILE *);
int fileno(FILE *);
size_t fread(void *, size_t, size_t, FILE *);
size_t fwrite(const void *, size_t, size_t, FILE *);
int fputc(int, FILE *);
int fputs(const char *, FILE *);
int fgetc(FILE *);
char *fgets(char *, int, FILE *);
int puts(const char *);
int putchar(int);
#define putc(c, f) fputc(c, f)
#define getc(f)    fgetc(f)
int fseek(FILE *, long, int);
long ftell(FILE *);
void rewind(FILE *);
int feof(FILE *);
int ferror(FILE *);
void clearerr(FILE *);
int fflush(FILE *);
int setvbuf(FILE *, char *, int, size_t);
void perror(const char *);
int remove(const char *);
int rename(const char *, const char *);
FILE *popen(const char *, const char *);
int pclose(FILE *);
