#pragma once
#include <stdint.h>
#include <inode.h>

typedef enum {
    FD_NONE  = 0,
    FD_INODE = 1,
} file_type_t;

struct file {
    file_type_t  type;
    int          refcnt;
    uint8_t      readable;
    uint8_t      writable;
    uint64_t     off;
    struct inode *ip;
};

#define NFILE  64   /* global open-file table size */
#define NOFILE 16   /* per-process fd table size   */

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
int          filestat(struct file *f, struct stat *st);
int          fileseek(struct file *f, int64_t off, int whence);
