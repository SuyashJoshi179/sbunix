#include <tarfs.h>
#include <string.h>

extern char _tarfs_start[], _tarfs_end[];

// Parse an octal ASCII string of length n.
static unsigned long parse_octal(const char *s, int n) {
    unsigned long val = 0;
    for (int i = 0; i < n && s[i] >= '0' && s[i] <= '7'; i++)
        val = val * 8 + (s[i] - '0');
    return val;
}

// Compare two null-terminated strings; return 1 if equal.
static int streq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

// Strip a leading "./" or "/" prefix from path, returning pointer into s.
static const char *strip_prefix(const char *s) {
    if (s[0] == '.' && s[1] == '/') return s + 2;
    if (s[0] == '/') return s + 1;
    return s;
}

const void *tarfs_find(const char *path, unsigned long *out_size) {
    const char *target = strip_prefix(path);

    char *p = _tarfs_start;
    while (p + 512 <= _tarfs_end) {
        struct tar_header *h = (struct tar_header *)p;

        // Two consecutive zero-filled blocks mark the end of the archive.
        if (h->name[0] == '\0') break;

        unsigned long size = parse_octal(h->size, 12);
        char *data = p + 512;

        // Regular file: typeflag '0' or NUL (old-style)
        if (h->typeflag == '0' || h->typeflag == '\0') {
            const char *name = strip_prefix(h->name);
            if (streq(name, target)) {
                if (out_size) *out_size = size;
                return data;
            }
        }

        // Advance past header + data (data is padded to 512-byte blocks)
        unsigned long data_blocks = (size + 511) / 512;
        p += 512 + data_blocks * 512;
    }
    return 0;
}
