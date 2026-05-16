#pragma once
#include <stdint.h>
#include <inode.h>

struct pipe;

typedef enum {
    FD_NONE  = 0,
    FD_INODE = 1,
    FD_PIPE  = 2,
} file_type_t;

struct file {
    file_type_t  type;
    int          refcnt;
    uint8_t      readable;
    uint8_t      writable;
    uint8_t      append;   /* O_APPEND: each write resets off to ip->size */
    uint64_t     off;
    struct inode *ip;
    struct pipe  *pipe;
};

#define NFILE  256  /* global open-file table size */
#define NOFILE 64   /* per-process fd table size   */

/* Seek whence values (match POSIX) */
#define SEEK_SET  0
#define SEEK_CUR  1
#define SEEK_END  2

struct stat;

struct file *filealloc(void);
struct file *filedup(struct file *f);
void         fileclose(struct file *f);
int          fileread(struct file *f, void *dst, uint64_t n);
int          filewrite(struct file *f, const void *src, uint64_t n);
int          filepread(struct file *f, void *dst, uint64_t n, uint64_t off);
int          filepwrite(struct file *f, const void *src, uint64_t n, uint64_t off);
int          filestat(struct file *f, struct stat *st);
int64_t      fileseek(struct file *f, int64_t off, int whence);
int          fileioctl(struct file *f, int cmd, unsigned long arg);
