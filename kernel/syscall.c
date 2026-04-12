#include <stdint.h>
#include <fs.h>
#include <file.h>
#include <proc.h>
#include <vmem.h>
#include <printk.h>
#include <string.h>
#include <syscall.h>
#include <drivers/uart.h>

/*
 * get_current() and proc_exit_current() are implemented in proc.c.
 * They will be wired up in the "minimal shared file changes" step:
 *   - proc.h  : add declaration of get_current() and proc_exit_current()
 *   - proc.h  : add  struct file *ofile[NOFILE];  to struct pcb
 *   - proc.c  : add  struct pcb *get_current(void) { return &procs[current_idx]; }
 *   - proc.c  : add  void proc_exit_current(void) { ... set state = PROC_UNUSED; }
 */
extern struct pcb     *get_current(void);
extern void            proc_exit_current(void);

/* ------------------------------------------------------------------ */
/* Address copy helpers                                                */
/*                                                                     */
/* In the current develop branch there are no user processes yet, so  */
/* all syscalls are invoked from kernel threads.  We therefore treat  */
/* every address as a kernel virtual address and copy directly.       */
/* When user-process support is merged, this can be updated to walk   */
/* the process page table (see the sbunix personal branch for the     */
/* full vmem_translate-based implementation).                         */
/* ------------------------------------------------------------------ */

static int copy_from_user(uint64_t addr, char *dst, int n) {
    memmove(dst, (char *)(unsigned long)addr, n);
    return n;
}

static int copy_to_user(uint64_t addr, char *src, int n) {
    memmove((char *)(unsigned long)addr, src, n);
    return n;
}

static int fetch_str(uint64_t addr, char *dst, int max) {
    char *src = (char *)(unsigned long)addr;
    for (int i = 0; i < max; i++) {
        dst[i] = src[i];
        if (src[i] == '\0') return i;
    }
    dst[max - 1] = '\0';
    return max - 1;
}

/* ------------------------------------------------------------------ */
/* File-descriptor helpers                                             */
/* ------------------------------------------------------------------ */

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

int64_t sys_write(int fd, uint64_t buf_uaddr, int n) {
    /* fd 1 and 2 → write directly to UART (console) */
    if (fd == STDOUT_FILENO || fd == STDERR_FILENO) {
        char kbuf[256];
        int done = 0;
        while (done < n) {
            int chunk = n - done;
            if (chunk > (int)sizeof(kbuf)) chunk = (int)sizeof(kbuf);
            copy_from_user(buf_uaddr + done, kbuf, chunk);
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
        if (chunk > (int)sizeof(kbuf)) chunk = (int)sizeof(kbuf);
        copy_from_user(buf_uaddr + done, kbuf, chunk);
        int w = filewrite(f, kbuf, chunk);
        if (w <= 0) return done > 0 ? done : -1;
        done += w;
    }
    return done;
}

int64_t sys_read(int fd, uint64_t buf_uaddr, int n) {
    struct file *f = fd_to_file(fd);
    if (f == 0) return -1;

    char kbuf[512];
    int done = 0;
    while (done < n) {
        int chunk = n - done;
        if (chunk > (int)sizeof(kbuf)) chunk = (int)sizeof(kbuf);
        int r = fileread(f, kbuf, chunk);
        if (r <= 0) break;
        copy_to_user(buf_uaddr + done, kbuf, r);
        done += r;
    }
    return done;
}

int64_t sys_open(uint64_t path_uaddr, int flags) {
    char path[128];
    if (fetch_str(path_uaddr, path, sizeof(path)) < 0) return -1;

    struct inode *ip = 0;

    if (flags & O_CREATE) {
        ip = namei(path);
        if (ip == 0)
            ip = ialloc(2);   /* create new regular-file inode */
        if (ip == 0) return -1;
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

int64_t sys_close(int fd) {
    struct pcb *p = get_current();
    if (p == 0) return -1;
    struct file *f = fd_to_file(fd);
    if (f == 0) return -1;
    p->ofile[fd] = 0;
    fileclose(f);
    return 0;
}

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
        case SYS_EXIT:  proc_exit_current(); return 0;
        case SYS_WRITE: return sys_write((int)a0, a1, (int)a2);
        case SYS_READ:  return sys_read ((int)a0, a1, (int)a2);
        case SYS_OPEN:  return sys_open (a0, (int)a1);
        case SYS_CLOSE: return sys_close((int)a0);
        case SYS_LS:    return sys_ls   (a0);
        default:
            printk("[syscall] unknown num=%ld\n", num);
            return -1;
    }
}
