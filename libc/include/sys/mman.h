#pragma once

#define PROT_NONE   0x0
#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define PROT_EXEC   0x4

#define MAP_SHARED  0x01
#define MAP_PRIVATE 0x02
#define MAP_ANON    0x20
#define MAP_ANONYMOUS MAP_ANON

#define MAP_FAILED ((void *)-1)

#ifndef MS_SYNC
#define MS_SYNC 0x4
#endif

void *mmap(void *addr, long len, int prot, int flags, int fd, long off);
int   munmap(void *addr, long len);
int   msync(void *addr, long len, int flags);
