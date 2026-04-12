#pragma once
#include <stdint.h>
#include <fs.h>

#define NFILE   64   /* global open-file table size */
#define NOFILE  16   /* max open files per process  */

/* open() flags */
#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREATE  0x200

/* well-known file descriptors */
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

struct file {
    int           ref;       /* reference count (0 = free slot) */
    char          readable;
    char          writable;
    struct inode *ip;        /* backing inode */
    uint32_t      off;       /* current byte offset */
};

void         fileinit(void);
struct file *filealloc(void);
struct file *filedup(struct file *f);
void         fileclose(struct file *f);
int          fileread(struct file *f, char *addr, int n);
int          filewrite(struct file *f, char *addr, int n);
