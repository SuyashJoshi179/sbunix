#include <file.h>
#include <fs.h>
#include <printk.h>
#include <string.h>

struct file ftable[NFILE];

void fileinit(void) {
    for (int i = 0; i < NFILE; i++)
        ftable[i].ref = 0;
    printk("[file] global file table initialized (%d slots)\n", NFILE);
}

/* Allocate a file structure from the global table. */
struct file *filealloc(void) {
    for (int i = 0; i < NFILE; i++) {
        if (ftable[i].ref == 0) {
            ftable[i].ref = 1;
            return &ftable[i];
        }
    }
    printk("[file] filealloc: no free slots\n");
    return 0;
}

/* Increment reference count. */
struct file *filedup(struct file *f) {
    if (f == 0 || f->ref < 1)
        return 0;
    f->ref++;
    return f;
}

/* Decrement reference count; free and release inode when it hits 0. */
void fileclose(struct file *f) {
    if (f == 0 || f->ref < 1) return;
    f->ref--;
    if (f->ref == 0) {
        struct inode *ip = f->ip;
        f->ip       = 0;
        f->readable = 0;
        f->writable = 0;
        f->off      = 0;
        if (ip) iput(ip);
    }
}

/* Read n bytes from f into addr. Returns bytes read, or -1 on error. */
int fileread(struct file *f, char *addr, int n) {
    if (f == 0 || !f->readable) return -1;
    int r = readi(f->ip, addr, f->off, (uint32_t)n);
    if (r > 0) f->off += (uint32_t)r;
    return r;
}

/* Write n bytes from addr into f. Returns bytes written, or -1 on error. */
int filewrite(struct file *f, char *addr, int n) {
    if (f == 0 || !f->writable) return -1;
    int w = writei(f->ip, addr, f->off, (uint32_t)n);
    if (w > 0) f->off += (uint32_t)w;
    return w;
}
