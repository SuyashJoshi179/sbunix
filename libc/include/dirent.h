#pragma once
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

struct dirent64 {
    uint64_t d_ino;
    uint64_t d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    char     d_name[];
};

/* POSIX-shaped dirent. d_name is fixed-size so user code can stack-allocate. */
struct dirent {
    uint64_t d_ino;
    int64_t  d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    char     d_name[256];
};

#define DT_UNKNOWN  0
#define DT_FIFO     1
#define DT_CHR      2
#define DT_DIR      4
#define DT_BLK      6
#define DT_REG      8
#define DT_LNK      10
#define DT_SOCK     12

typedef struct __DIR DIR;

DIR           *opendir(const char *path);
DIR           *fdopendir(int fd);
struct dirent *readdir(DIR *dir);
int            readdir_r(DIR *dir, struct dirent *ent, struct dirent **result);
int            closedir(DIR *dir);
void           rewinddir(DIR *dir);
int            dirfd(DIR *dir);
long           telldir(DIR *dir);
void           seekdir(DIR *dir, long off);

long           getdents64(int fd, void *buf, long n);
