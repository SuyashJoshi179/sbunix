#include <tarfs.h>
#include <string.h>

#define TAR_BLOCK_SIZE 512UL

struct tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
};

extern char _tarfs_start[];
extern char _tarfs_end[];

static const char *tar_begin;
static const char *tar_end;

static bool is_zero_block(const char *blk) {
    for (unsigned long i = 0; i < TAR_BLOCK_SIZE; i++) {
        if (blk[i] != 0) {
            return false;
        }
    }
    return true;
}

static unsigned long parse_octal(const char *s, unsigned long n) {
    unsigned long v = 0;

    for (unsigned long i = 0; i < n; i++) {
        char c = s[i];
        if (c == 0 || c == ' ') {
            break;
        }
        if (c < '0' || c > '7') {
            break;
        }
        v = (v << 3) + (unsigned long)(c - '0');
    }

    return v;
}

static unsigned long min_ul(unsigned long a, unsigned long b) {
    return a < b ? a : b;
}

static unsigned long cstr_len(const char *s, unsigned long maxn) {
    unsigned long n = 0;
    while (n < maxn && s[n] != 0) {
        n++;
    }
    return n;
}

static const char *skip_path_prefixes(const char *s) {
    while (s[0] == '/') {
        s++;
    }

    while (s[0] == '.' && s[1] == '/') {
        s += 2;
        while (s[0] == '/') {
            s++;
        }
    }

    return s;
}

static bool path_match(const struct tar_header *h, const char *path) {
    unsigned long pi = 0;
    unsigned long pref_len = cstr_len(h->prefix, sizeof(h->prefix));
    unsigned long name_len = cstr_len(h->name, sizeof(h->name));
    const char *p = skip_path_prefixes(path);

    for (unsigned long i = 0; i < pref_len; i++, pi++) {
        if (p[pi] != h->prefix[i]) {
            return false;
        }
    }

    if (pref_len != 0) {
        if (p[pi] != '/') {
            return false;
        }
        pi++;
    }

    const char *name = skip_path_prefixes(h->name);
    if (name != h->name) {
        name_len = cstr_len(name, sizeof(h->name));
    }

    for (unsigned long i = 0; i < name_len; i++, pi++) {
        if (p[pi] != name[i]) {
            return false;
        }
    }

    return p[pi] == 0;
}

void tarfs_init(void) {
    tar_begin = _tarfs_start;
    tar_end = _tarfs_end;
}

bool tarfs_lookup(const char *path, struct tarfs_node *out) {
    const char *p = tar_begin;

    if (p == 0 || tar_end == 0 || p >= tar_end || path == 0 || out == 0) {
        return false;
    }

    while (p + TAR_BLOCK_SIZE <= tar_end) {
        const struct tar_header *h = (const struct tar_header *)p;

        if (is_zero_block(p)) {
            return false;
        }

        unsigned long fsz = parse_octal(h->size, sizeof(h->size));
        unsigned long data_blocks = (fsz + TAR_BLOCK_SIZE - 1) / TAR_BLOCK_SIZE;
        const char *data = p + TAR_BLOCK_SIZE;
        const char *next = data + data_blocks * TAR_BLOCK_SIZE;

        if (next > tar_end) {
            return false;
        }

        if ((h->typeflag == '0' || h->typeflag == 0) && path_match(h, path)) {
            out->data = data;
            out->size = fsz;
            return true;
        }

        p = next;
    }

    return false;
}

unsigned long tarfs_read(const struct tarfs_node *node, unsigned long off, void *dst, unsigned long len) {
    if (node == 0 || dst == 0 || off >= node->size || len == 0) {
        return 0;
    }

    unsigned long avail = node->size - off;
    unsigned long n = min_ul(len, avail);
    memmove(dst, node->data + off, n);
    return n;
}
