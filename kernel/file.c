#include <file.h>
#include <inode.h>
#include <pipe.h>
#include <stat.h>
#include <errno.h>
#include <riscv.h>
#include <printk.h>
#include <string.h>

// Global open-file table.
static struct file ftable[NFILE];

// IRQs-off is our only lock on the single-hart system.
static void file_lock(void)   { write_sstatus(read_sstatus() & ~SSTATUS_SIE); }
static void file_unlock(void) { write_sstatus(read_sstatus() |  SSTATUS_SIE); }

struct inode *inode_get(struct inode *ip) {
    if (ip) ip->refcnt++;
    return ip;
}

void inode_put(struct inode *ip) {
    if (!ip) return;
    if (--ip->refcnt == 0 && ip->ops && ip->ops->release)
        ip->ops->release(ip);
}

// Allocate a file from the global table (refcnt = 1, caller fills fields).
struct file *filealloc(void) {
    file_lock();
    for (int i = 0; i < NFILE; i++) {
        if (ftable[i].type == FD_NONE) {
            ftable[i].type   = FD_INODE;  // placeholder; caller may adjust
            ftable[i].refcnt = 1;
            ftable[i].append = 0;
            file_unlock();
            return &ftable[i];
        }
    }
    file_unlock();
    return 0;
}

// Bump refcount; returns f for convenience.
struct file *filedup(struct file *f) {
    file_lock();
    if (f->refcnt < 1) panic("filedup: bad refcnt");
    f->refcnt++;
    file_unlock();
    return f;
}

// Decrement refcount; release if it reaches 0.
void fileclose(struct file *f) {
    file_lock();
    if (f->refcnt < 1) panic("fileclose: bad refcnt");
    int do_free = (--f->refcnt == 0);
    file_unlock();

    if (do_free) {
        if (f->type == FD_PIPE) {
            pipe_close(f->pipe, f->writable);
            f->pipe = 0;
        } else {
            inode_put(f->ip);
            f->ip = 0;
        }
        f->type = FD_NONE;
    }
}

int fileread(struct file *f, void *dst, uint64_t n) {
    if (!f->readable) return -EBADF;
    if (f->type == FD_PIPE)
        return pipe_read(f->pipe, (char *)dst, (int)n);
    if (f->type != FD_INODE || !f->ip || !f->ip->ops->read) return -EBADF;
    int r = f->ip->ops->read(f->ip, f->off, dst, n);
    if (r > 0) f->off += (uint64_t)r;
    return r;
}

int filewrite(struct file *f, const void *src, uint64_t n) {
    if (!f->writable) return -EBADF;
    if (f->type == FD_PIPE)
        return pipe_write(f->pipe, (const char *)src, (int)n);
    if (f->type != FD_INODE || !f->ip || !f->ip->ops->write) return -EBADF;
    /* POSIX O_APPEND: each write must atomically reposition to EOF
     * before writing — not just the first one. Without this, lseek()
     * (or another fd extending the file) leaves writes landing at the
     * stale offset. */
    if (f->append) f->off = f->ip->size;
    int w = f->ip->ops->write(f->ip, f->off, src, n);
    if (w > 0) f->off += (uint64_t)w;
    return w;
}

int filestat(struct file *f, struct stat *st) {
    if (f->type == FD_PIPE) {
        memset(st, 0, sizeof(*st));
        st->st_mode = 0010000;  /* S_IFIFO */
        return 0;
    }
    if (f->type != FD_INODE || !f->ip || !f->ip->ops->stat) return -EBADF;
    return f->ip->ops->stat(f->ip, st);
}

int64_t fileseek(struct file *f, int64_t off, int whence) {
    if (f->type == FD_PIPE) return -ESPIPE;
    if (f->type != FD_INODE) return -ESPIPE;
    // Character devices are not seekable.
    if (f->ip && f->ip->type == I_CHR) return -ESPIPE;

    int64_t base;
    switch (whence) {
        case SEEK_SET: base = 0;                      break;
        case SEEK_CUR: base = (int64_t)f->off;        break;
        case SEEK_END: base = (int64_t)f->ip->size;   break;
        default:       return -EINVAL;
    }
    int64_t newoff = base + off;
    if (newoff < 0) return -EINVAL;
    f->off = (uint64_t)newoff;
    return newoff;
}

int fileioctl(struct file *f, int cmd, unsigned long arg) {
    if (!f) return -EBADF;
    if (f->type != FD_INODE || !f->ip) return -ENOTTY;
    if (!f->ip->ops || !f->ip->ops->ioctl) return -ENOTTY;
    return f->ip->ops->ioctl(f->ip, cmd, arg);
}
