#include <tarfs.h>
#include <printk.h>
#include <string.h>

/* Symbols injected by objcopy when tarfs.o is linked */
extern char _tarfs_start[];
extern char _tarfs_end[];

/* USTAR header (512 bytes per block) */
struct tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];    /* octal string */
    char mtime[12];
    char checksum[8];
    char typeflag;    /* '0' = regular file, '5' = directory */
    char linkname[100];
    char magic[6];    /* "ustar" */
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
};  /* total: 512 bytes */

static unsigned long octal_str(const char *s, int n) {
    unsigned long v = 0;
    for (int i = 0; i < n && s[i] >= '0' && s[i] <= '7'; i++)
        v = v * 8 + (s[i] - '0');
    return v;
}

char *tarfs_find(const char *name, unsigned long *size_out) {
    char *p   = _tarfs_start;
    char *end = _tarfs_end;

    while (p + 512 <= end) {
        struct tar_header *h = (struct tar_header *)p;

        /* Two consecutive zero blocks = end of archive */
        if (h->name[0] == '\0') break;

        unsigned long fsize = octal_str(h->size, 12);

        /* Match: accept both "bin/ls" and "./bin/ls" */
        if (h->typeflag == '0' || h->typeflag == '\0') {
            const char *n = h->name;
            if (n[0] == '.' && n[1] == '/') n += 2;
            if (strncmp(n, name, 100) == 0) {
                if (size_out) *size_out = fsize;
                return p + 512;   /* data immediately follows header */
            }
        }

        /* Advance past header + data (round data up to 512-byte boundary) */
        p += 512 + ((fsize + 511) & ~511UL);
    }
    return 0;
}
