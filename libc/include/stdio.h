#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>
#include <stdarg.h>

#ifndef EOF
#define EOF (-1)
#endif

#define BUFSIZ 1024

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

typedef struct _FILE FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int   printf(const char *fmt, ...);
int   fprintf(FILE *stream, const char *fmt, ...);
int   sprintf(char *buf, const char *fmt, ...);
int   snprintf(char *buf, size_t n, const char *fmt, ...);
int   vprintf(const char *fmt, va_list ap);
int   vfprintf(FILE *stream, const char *fmt, va_list ap);
int   vsprintf(char *buf, const char *fmt, va_list ap);
int   vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);

int   puts(const char *s);
int   fputs(const char *s, FILE *stream);
int   putchar(int c);
int   putc(int c, FILE *stream);
int   fputc(int c, FILE *stream);
int   getchar(void);
int   getc(FILE *stream);
int   fgetc(FILE *stream);
char *fgets(char *buf, int n, FILE *stream);

FILE  *fopen(const char *path, const char *mode);
int    fclose(FILE *stream);
int    fflush(FILE *stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int    fseek(FILE *stream, long off, int whence);
long   ftell(FILE *stream);
void   rewind(FILE *stream);
int    feof(FILE *stream);
int    ferror(FILE *stream);
void   clearerr(FILE *stream);
int    fileno(FILE *stream);

void   perror(const char *s);
int    remove(const char *path);
int    rename(const char *oldpath, const char *newpath);

#endif
