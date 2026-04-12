#include <stdint.h>
#include <fs.h>
#include <file.h>
#include <proc.h>
#include <vmem.h>
#include <printk.h>
#include <string.h>
#include <syscall.h>
#include <drivers/uart.h>

/* ------------------------------------------------------------------ */
/* User-memory helpers                                                 */
/*                                                                     */
/* Syscall args that are pointers refer to user virtual addresses.     */
/* We must translate them through the current process's page table.    */
/* Kernel threads have pagetable==0 → treat address as kernel-virtual. */
/* ------------------------------------------------------------------ */

static int copy_from_user(uint64_t uaddr, char *dst, int n) {
    struct pcb *p = get_current();
    if (p == 0 || p->pagetable == 0) {
        memmove(dst, (char *)(unsigned long)uaddr, n);
        return n;
    }
    for (int i = 0; i < n; ) {
        uint64_t page_base = (uaddr + i) & ~0xFFFUL;
        uint64_t page_off  = (uaddr + i) &  0xFFFUL;
        unsigned long phys = vmem_translate(p->pagetable, page_base);
        if (phys == 0) return -1;
        char *kp    = (char *)phys_to_virt(phys);
        int   chunk = 4096 - (int)page_off;
        if (chunk > n - i) chunk = n - i;
        memmove(dst + i, kp + page_off, chunk);
        i += chunk;
    }
    return n;
}

static int copy_to_user(uint64_t uaddr, char *src, int n) {
    struct pcb *p = get_current();
    if (p == 0 || p->pagetable == 0) {
        memmove((char *)(unsigned long)uaddr, src, n);
        return n;
    }
    for (int i = 0; i < n; ) {
        uint64_t page_base = (uaddr + i) & ~0xFFFUL;
        uint64_t page_off  = (uaddr + i) &  0xFFFUL;
        unsigned long phys = vmem_translate(p->pagetable, page_base);
        if (phys == 0) return -1;
        char *kp    = (char *)phys_to_virt(phys);
        int   chunk = 4096 - (int)page_off;
        if (chunk > n - i) chunk = n - i;
        memmove(kp + page_off, src + i, chunk);
        i += chunk;
    }
    return n;
}

/* Copy a NUL-terminated string from user space. Returns length, -1 on error. */
static int fetch_str(uint64_t uaddr, char *dst, int max) {
    for (int i = 0; i < max; i++) {
        if (copy_from_user(uaddr + i, dst + i, 1) < 0) return -1;
        if (dst[i] == '\0') return i;
    }
    dst[max - 1] = '\0';
    return max - 1;
}

/* ------------------------------------------------------------------ */
/* File-descriptor helpers                                             */
/* ------------------------------------------------------------------ */

/* Allocate the lowest free fd slot in the current process. */
static int fdalloc(struct file *f) {
    struct pcb *p = get_current();
    if (p == 0) return -1;
    for (int i = 0; i < NOFILE; i++) {
        if (p->ofile[i] == 0) {
            p->ofile[i] = f;
            return i;
        }
    }
    return -1;
}

static struct file *fd_to_file(int fd) {
    struct pcb *p = get_current();
    if (p == 0 || fd < 0 || fd >= NOFILE) return 0;
    return p->ofile[fd];
}

/* ------------------------------------------------------------------ */
/* Syscall implementations                                             */
/* ------------------------------------------------------------------ */

/*
 * sys_write(fd, buf_uaddr, n)
 *   fd=1/2 → write directly to UART (console)
 *   other  → write to the file's inode
 */
int64_t sys_write(int fd, uint64_t buf_uaddr, int n) {
    if (fd == STDOUT_FILENO || fd == STDERR_FILENO) {
        char kbuf[256];
        int done = 0;
        while (done < n) {
            int chunk = n - done;
            if (chunk > (int)sizeof(kbuf)) chunk = sizeof(kbuf);
            if (copy_from_user(buf_uaddr + done, kbuf, chunk) < 0) return -1;
            for (int i = 0; i < chunk; i++) write_char(kbuf[i]);
            done += chunk;
        }
        return n;
    }

    struct file *f = fd_to_file(fd);
    if (f == 0) return -1;

    char kbuf[512];
    int done = 0;
    while (done < n) {
        int chunk = n - done;
        if (chunk > (int)sizeof(kbuf)) chunk = sizeof(kbuf);
        if (copy_from_user(buf_uaddr + done, kbuf, chunk) < 0) return -1;
        int w = filewrite(f, kbuf, chunk);
        if (w <= 0) return done > 0 ? done : -1;
        done += w;
    }
    return done;
}

/*
 * sys_read(fd, buf_uaddr, n)
 */
int64_t sys_read(int fd, uint64_t buf_uaddr, int n) {
    struct file *f = fd_to_file(fd);
    if (f == 0) return -1;

    char kbuf[512];
    int done = 0;
    while (done < n) {
        int chunk = n - done;
        if (chunk > (int)sizeof(kbuf)) chunk = sizeof(kbuf);
        int r = fileread(f, kbuf, chunk);
        if (r <= 0) break;
        if (copy_to_user(buf_uaddr + done, kbuf, r) < 0) return -1;
        done += r;
    }
    return done;
}

/*
 * sys_open(path_uaddr, flags)
 *   O_CREATE: allocate a new inode if path not found
 *   Returns fd, or -1 on error.
 */
int64_t sys_open(uint64_t path_uaddr, int flags) {
    char path[128];
    if (fetch_str(path_uaddr, path, sizeof(path)) < 0) return -1;

    struct inode *ip = 0;

    if (flags & O_CREATE) {
        ip = namei(path);
        if (ip == 0) {
            /* File doesn't exist — create a new inode (type=2: regular file).
             * Full dirlink into parent directory comes in Layer 6 with mkfs
             * setting up the root dir.  For now we just allocate the inode. */
            ip = ialloc(2);
            if (ip == 0) return -1;
        }
    } else {
        ip = namei(path);
        if (ip == 0) return -1;
    }

    struct file *f = filealloc();
    if (f == 0) { iput(ip); return -1; }

    f->ip       = ip;
    f->off      = 0;
    f->readable = (flags & O_WRONLY) ? 0 : 1;
    f->writable = (flags & O_RDONLY) ? 0 : 1;
    if ((flags & O_RDWR) == O_RDWR) { f->readable = 1; f->writable = 1; }

    int fd = fdalloc(f);
    if (fd < 0) { fileclose(f); return -1; }

    printk("[sys_open] path=\"%s\" flags=%d → fd=%d inum=%d\n",
           path, flags, fd, ip->inum);
    return fd;
}

/*
 * sys_close(fd)
 */
int64_t sys_close(int fd) {
    struct pcb *p = get_current();
    if (p == 0) return -1;
    struct file *f = fd_to_file(fd);
    if (f == 0) return -1;
    p->ofile[fd] = 0;
    fileclose(f);
    return 0;
}

/*
 * sys_ls(path_uaddr)
 *   Lists the directory at path, printing each entry.
 */
int64_t sys_ls(uint64_t path_uaddr) {
    char path[128];
    if (fetch_str(path_uaddr, path, sizeof(path)) < 0) return -1;

    struct inode *dp = namei(path);
    if (dp == 0) {
        printk("[sys_ls] path not found: %s\n", path);
        return -1;
    }

    printk("[sys_ls] listing \"%s\":\n", path);
    struct dirent de;
    for (uint32_t off = 0; off < dp->size; off += sizeof(de)) {
        if (readi(dp, (char *)&de, off, sizeof(de)) != (int)sizeof(de)) break;
        if (de.inum == 0) continue;
        printk("  %s  (inum=%d)\n", de.name, de.inum);
    }

    iput(dp);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Dispatcher — called from trap.c                                     */
/* ------------------------------------------------------------------ */

int64_t syscall_handler(uint64_t num,
                        uint64_t a0, uint64_t a1, uint64_t a2) {
    switch (num) {
        case SYS_EXIT:
            proc_exit_current();
            return 0;
        case SYS_WRITE:
            return sys_write((int)a0, a1, (int)a2);
        case SYS_READ:
            return sys_read((int)a0, a1, (int)a2);
        case SYS_OPEN:
            return sys_open(a0, (int)a1);
        case SYS_CLOSE:
            return sys_close((int)a0);
        case SYS_LS:
            return sys_ls(a0);
        default:
            printk("[syscall] unknown num=%ld\n", num);
            return -1;
    }
}
