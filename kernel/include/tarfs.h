#pragma once
#include <stdint.h>
#include <inode.h>

// POSIX ustar tar header (512 bytes per block).
struct tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];      // file size in octal ASCII
    char mtime[12];
    char checksum[8];
    char typeflag;      // '0'/'\0' = regular file, '5' = directory
    char linkname[100];
    char magic[6];      // "ustar"
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];       // padding to 512 bytes
};

// Build the inode tree from the embedded tar archive and mount it at "/".
// Must be called before namei() or proc_spawn().
void tarfs_init(void);

// Root inode of the tarfs tree (set by tarfs_init).
extern struct inode *tarfs_root;

// Find a file by path in the embedded tar archive (legacy flat lookup).
// Still used by the selftest; exec now goes through namei().
const void *tarfs_find(const char *path, unsigned long *out_size);
