#pragma once
#include <stdint.h>

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

// Find a file by path in the embedded tar archive.
// path may begin with '/' or not (e.g. "bin/init" or "/bin/init").
// Returns a pointer to the file data and sets *out_size on success.
// Returns NULL if the path is not found.
const void *tarfs_find(const char *path, unsigned long *out_size);
