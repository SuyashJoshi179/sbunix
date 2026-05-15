#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>
#include <stdarg.h>
#include <sys/types.h>

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
ssize_t getline(char **lineptr, size_t *n, FILE *stream);
ssize_t getdelim(char **lineptr, size_t *n, int delim, FILE *stream);

int   sscanf(const char *str, const char *fmt, ...);
int   fscanf(FILE *stream, const char *fmt, ...);
int   scanf(const char *fmt, ...);
int   vsscanf(const char *str, const char *fmt, va_list ap);
int   vfscanf(FILE *stream, const char *fmt, va_list ap);
int   vscanf(const char *fmt, va_list ap);

int   asprintf(char **strp, const char *fmt, ...);
int   vasprintf(char **strp, const char *fmt, va_list ap);
int   dprintf(int fd, const char *fmt, ...);
int   vdprintf(int fd, const char *fmt, va_list ap);

/* glibc stdio _unlocked variants — single-threaded SBUnix has no locks,
 * so these are simple aliases for the locked forms. */
int   fputs_unlocked(const char *s, FILE *stream);
int   putc_unlocked(int c, FILE *stream);
int   getc_unlocked(FILE *stream);
int   fgetc_unlocked(FILE *stream);
int   fputc_unlocked(int c, FILE *stream);
int   feof_unlocked(FILE *stream);
int   ferror_unlocked(FILE *stream);
int   fileno_unlocked(FILE *stream);

FILE  *fopen(const char *path, const char *mode);
int    fclose(FILE *stream);
int    fflush(FILE *stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int    fseek(FILE *stream, long off, int whence);
long   ftell(FILE *stream);
int    fseeko(FILE *stream, off_t off, int whence);
off_t  ftello(FILE *stream);
void   rewind(FILE *stream);
int    feof(FILE *stream);
int    ferror(FILE *stream);
void   clearerr(FILE *stream);
int    fileno(FILE *stream);

void   perror(const char *s);
int    remove(const char *path);
int    rename(const char *oldpath, const char *newpath);

int    ungetc(int c, FILE *stream);
void   setbuf(FILE *stream, char *buf);
int    setvbuf(FILE *stream, char *buf, int mode, size_t size);
char  *tmpnam(char *s);
FILE  *tmpfile(void);

#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2

#define L_tmpnam 32
#define FILENAME_MAX 4096
#define FOPEN_MAX 64
#define TMP_MAX 1

#endif
