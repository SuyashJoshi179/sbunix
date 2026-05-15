/*
 * POSIX conformance test: <stdio.h>
 *
 * Reference: docs/susv5-html/basedefs/stdio.h.html
 *
 * Audited POSIX functions (only those our libc declares):
 *   fopen, fclose, fflush, fread, fwrite, fseek, fseeko, ftell, rewind,
 *   feof, ferror, clearerr, fileno, fgetc, fgets, fputc, fputs, getc,
 *   getchar, putc, putchar, puts, ungetc, fprintf, printf, snprintf,
 *   sprintf, vfprintf, vprintf, vsnprintf, vsprintf, fscanf, scanf,
 *   sscanf, vfscanf, vscanf, vsscanf, dprintf, vdprintf, perror, remove,
 *   rename, setbuf, setvbuf, tmpnam, getline, getdelim
 *
 * Excluded (POSIX not in our libc):
 *   ctermid, fdopen, fgetpos, flockfile, fmemopen, freopen, fsetpos,
 *   ftello, ftrylockfile, funlockfile, getchar_unlocked, open_memstream,
 *   pclose, popen, putchar_unlocked, renameat, tmpfile
 *
 * Excluded (non-POSIX): asprintf, vasprintf, *_unlocked variants
 *
 * Required macros: BUFSIZ, EOF, FILENAME_MAX, FOPEN_MAX, L_tmpnam,
 *                  SEEK_SET/CUR/END, TMP_MAX, _IOFBF, _IOLBF, _IONBF
 * Required globals: stdin, stdout, stderr (FILE *)
 */
#include <stdio.h>

#define PIN __attribute__((unused)) static

/* File ops */
PIN FILE   *(*_pin_fopen)(const char *, const char *) = fopen;
PIN int     (*_pin_fclose)(FILE *) = fclose;
PIN int     (*_pin_fflush)(FILE *) = fflush;
PIN size_t  (*_pin_fread)(void *, size_t, size_t, FILE *) = fread;
PIN size_t  (*_pin_fwrite)(const void *, size_t, size_t, FILE *) = fwrite;
PIN int     (*_pin_fseek)(FILE *, long, int) = fseek;
PIN int     (*_pin_fseeko)(FILE *, off_t, int) = fseeko;
PIN long    (*_pin_ftell)(FILE *) = ftell;
PIN void    (*_pin_rewind)(FILE *) = rewind;
PIN int     (*_pin_feof)(FILE *) = feof;
PIN int     (*_pin_ferror)(FILE *) = ferror;
PIN void    (*_pin_clearerr)(FILE *) = clearerr;
PIN int     (*_pin_fileno)(FILE *) = fileno;
PIN int     (*_pin_ungetc)(int, FILE *) = ungetc;
PIN void    (*_pin_setbuf)(FILE *, char *) = setbuf;
PIN int     (*_pin_setvbuf)(FILE *, char *, int, size_t) = setvbuf;
PIN int     (*_pin_remove)(const char *) = remove;
PIN int     (*_pin_rename)(const char *, const char *) = rename;
PIN char   *(*_pin_tmpnam)(char *) = tmpnam;

/* Character I/O */
PIN int     (*_pin_fgetc)(FILE *) = fgetc;
PIN char   *(*_pin_fgets)(char *, int, FILE *) = fgets;
PIN int     (*_pin_fputc)(int, FILE *) = fputc;
PIN int     (*_pin_fputs)(const char *, FILE *) = fputs;
PIN int     (*_pin_getc)(FILE *) = getc;
PIN int     (*_pin_getchar)(void) = getchar;
PIN int     (*_pin_putc)(int, FILE *) = putc;
PIN int     (*_pin_putchar)(int) = putchar;
PIN int     (*_pin_puts)(const char *) = puts;

/* Formatted I/O — variadic and va_list forms. */
PIN int     (*_pin_printf)(const char *, ...) = printf;
PIN int     (*_pin_fprintf)(FILE *, const char *, ...) = fprintf;
PIN int     (*_pin_sprintf)(char *, const char *, ...) = sprintf;
PIN int     (*_pin_snprintf)(char *, size_t, const char *, ...) = snprintf;
PIN int     (*_pin_vprintf)(const char *, va_list) = vprintf;
PIN int     (*_pin_vfprintf)(FILE *, const char *, va_list) = vfprintf;
PIN int     (*_pin_vsprintf)(char *, const char *, va_list) = vsprintf;
PIN int     (*_pin_vsnprintf)(char *, size_t, const char *, va_list) = vsnprintf;
PIN int     (*_pin_dprintf)(int, const char *, ...) = dprintf;
PIN int     (*_pin_vdprintf)(int, const char *, va_list) = vdprintf;
PIN int     (*_pin_scanf)(const char *, ...) = scanf;
PIN int     (*_pin_fscanf)(FILE *, const char *, ...) = fscanf;
PIN int     (*_pin_sscanf)(const char *, const char *, ...) = sscanf;
PIN int     (*_pin_vscanf)(const char *, va_list) = vscanf;
PIN int     (*_pin_vfscanf)(FILE *, const char *, va_list) = vfscanf;
PIN int     (*_pin_vsscanf)(const char *, const char *, va_list) = vsscanf;

PIN void    (*_pin_perror)(const char *) = perror;

PIN ssize_t (*_pin_getline)(char **, size_t *, FILE *) = getline;
PIN ssize_t (*_pin_getdelim)(char **, size_t *, int, FILE *) = getdelim;

/* FILE * globals */
PIN FILE  **_pin_stdin  = &stdin;
PIN FILE  **_pin_stdout = &stdout;
PIN FILE  **_pin_stderr = &stderr;

__attribute__((unused))
static void _macro_checks(void) {
    int x = SEEK_SET | SEEK_CUR | SEEK_END;
    x |= _IOFBF | _IOLBF | _IONBF;
    (void)x;
    (void)EOF; (void)BUFSIZ; (void)FILENAME_MAX;
    (void)FOPEN_MAX; (void)L_tmpnam; (void)TMP_MAX;
    (void)NULL;
}
