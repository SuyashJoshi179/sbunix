#include <errno.h>
#include <exec.h>
#include <file.h>
#include <inode.h>
#include <printk.h>
#include <proc.h>
#include <riscv.h>
#include <stat.h>
#include <stdint.h>
#include <string.h>
#include <syscall.h>
#include <tarfs.h>
#include <vfs.h>
#include <vmem.h>
#include <drivers/uart.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Validate a user pointer: must be non-null and below the kernel base.
static int uptr_ok(const void *p) {
    return p && (unsigned long)p < KVMEM_OFFSET;
}

// Allocate the lowest free fd slot in the current process.
// Returns the fd number or -EMFILE if the table is full.
static int alloc_fd(struct pcb *p, struct file *f) {
    for (int fd = 0; fd < NOFILE; fd++) {
        if (!p->ofile[fd]) {
            p->ofile[fd] = f;
            return fd;
        }
    }
    return -EMFILE;
}

// ---------------------------------------------------------------------------
// sys_exit
// ---------------------------------------------------------------------------
static int64_t sys_exit(int status) {
    proc_exit_current(status);
    return 0;
}

// ---------------------------------------------------------------------------
// sys_write — write through the FD table
//
// Falls back to direct UART for fd 1/2 if the process has no ofile table
// (kernel threads, early-boot selftests).
// ---------------------------------------------------------------------------
static int64_t sys_write(int fd, const char *buf, uint64_t len) {
    if (!uptr_ok(buf)) return -EFAULT;

    struct pcb *p = current_proc();
    if (p && fd >= 0 && fd < NOFILE && p->ofile[fd]) {
        return filewrite(p->ofile[fd], buf, len);
    }

    // Fallback: direct UART for fd 1/2 (handles early boot and kernel threads).
    if (fd == 1 || fd == 2) {
        for (uint64_t i = 0; i < len; i++) write_char(buf[i]);
        return (int64_t)len;
    }
    return -EBADF;
}

// ---------------------------------------------------------------------------
// sys_read — read through the FD table
// ---------------------------------------------------------------------------
static int64_t sys_read(int fd, void *buf, uint64_t len) {
    if (!uptr_ok(buf)) return -EFAULT;

    struct pcb *p = current_proc();
    if (!p) return -EBADF;
    if (fd < 0 || fd >= NOFILE || !p->ofile[fd]) return -EBADF;

    return fileread(p->ofile[fd], buf, len);
}

// ---------------------------------------------------------------------------
// sys_open
// ---------------------------------------------------------------------------
static int64_t sys_open(const char *path, int flags) {
    if (!uptr_ok(path)) return -EFAULT;

    struct inode *ip;
    int rc = namei(path, &ip);
    if (rc < 0) return rc;

    struct file *f = filealloc();
    if (!f) { inode_put(ip); return -EMFILE; }

    f->type     = FD_INODE;
    f->ip       = ip;  // namei already bumped refcnt
    f->off      = 0;
    f->readable = ((flags & 3) == 0 || (flags & 3) == 2) ? 1 : 0; // O_RDONLY or O_RDWR
    f->writable = ((flags & 3) == 1 || (flags & 3) == 2) ? 1 : 0; // O_WRONLY or O_RDWR
    // Character devices are always writable.
    if (ip->type == I_CHR) { f->readable = 1; f->writable = 1; }
    if (flags & 02000 /* O_APPEND */) f->off = ip->size;

    struct pcb *p = current_proc();
    if (!p) { fileclose(f); return -EBADF; }

    int fd = alloc_fd(p, f);
    if (fd < 0) { fileclose(f); return fd; }
    return fd;
}

// ---------------------------------------------------------------------------
// sys_close
// ---------------------------------------------------------------------------
static int64_t sys_close(int fd) {
    struct pcb *p = current_proc();
    if (!p || fd < 0 || fd >= NOFILE || !p->ofile[fd]) return -EBADF;
    fileclose(p->ofile[fd]);
    p->ofile[fd] = 0;
    return 0;
}

// ---------------------------------------------------------------------------
// sys_dup
// ---------------------------------------------------------------------------
static int64_t sys_dup(int fd) {
    struct pcb *p = current_proc();
    if (!p || fd < 0 || fd >= NOFILE || !p->ofile[fd]) return -EBADF;
    struct file *f = filedup(p->ofile[fd]);
    int newfd = alloc_fd(p, f);
    if (newfd < 0) { fileclose(f); return newfd; }
    return newfd;
}

// ---------------------------------------------------------------------------
// sys_dup2
// ---------------------------------------------------------------------------
static int64_t sys_dup2(int oldfd, int newfd) {
    struct pcb *p = current_proc();
    if (!p) return -EBADF;
    if (oldfd < 0 || oldfd >= NOFILE || !p->ofile[oldfd]) return -EBADF;
    if (newfd < 0 || newfd >= NOFILE) return -EBADF;

    if (oldfd == newfd) return newfd;

    if (p->ofile[newfd]) fileclose(p->ofile[newfd]);
    p->ofile[newfd] = filedup(p->ofile[oldfd]);
    return newfd;
}

// ---------------------------------------------------------------------------
// sys_lseek
// ---------------------------------------------------------------------------
static int64_t sys_lseek(int fd, int64_t off, int whence) {
    struct pcb *p = current_proc();
    if (!p || fd < 0 || fd >= NOFILE || !p->ofile[fd]) return -EBADF;
    return fileseek(p->ofile[fd], off, whence);
}

// ---------------------------------------------------------------------------
// sys_fstat
// ---------------------------------------------------------------------------
static int64_t sys_fstat(int fd, struct stat *st) {
    if (!uptr_ok(st)) return -EFAULT;
    struct pcb *p = current_proc();
    if (!p || fd < 0 || fd >= NOFILE || !p->ofile[fd]) return -EBADF;
    return filestat(p->ofile[fd], st);
}

// ---------------------------------------------------------------------------
// sys_getdents64
// ---------------------------------------------------------------------------
static int64_t sys_getdents64(int fd, void *buf, uint64_t n) {
    if (!uptr_ok(buf)) return -EFAULT;
    struct pcb *p = current_proc();
    if (!p || fd < 0 || fd >= NOFILE || !p->ofile[fd]) return -EBADF;

    struct file *f = p->ofile[fd];
    if (f->type != FD_INODE || !f->ip || f->ip->type != I_DIR) return -ENOTDIR;

    uint64_t next;
    int r = f->ip->ops->getdents(f->ip, f->off, buf, n, &next);
    if (r > 0) f->off = next;
    return r;
}

// ---------------------------------------------------------------------------
// sys_chdir
// ---------------------------------------------------------------------------
static int64_t sys_chdir(const char *path) {
    if (!uptr_ok(path)) return -EFAULT;

    struct inode *ip;
    int rc = namei(path, &ip);
    if (rc < 0) return rc;
    if (ip->type != I_DIR) { inode_put(ip); return -ENOTDIR; }

    struct pcb *p = current_proc();
    if (!p) { inode_put(ip); return -EBADF; }

    if (p->cwd) inode_put(p->cwd);
    p->cwd = ip;

    // Update cwd_path string.
    // Normalize: for simplicity just store the requested path if absolute,
    // otherwise recompute from parent path + "/" + component.
    if (path[0] == '/') {
        int i = 0;
        while (path[i] && i < 254) { p->cwd_path[i] = path[i]; i++; }
        p->cwd_path[i] = '\0';
    } else {
        // Relative: append to existing cwd_path.
        int base = 0;
        while (p->cwd_path[base]) base++;
        if (base > 1) { p->cwd_path[base] = '/'; base++; } // avoid double /
        int i = 0;
        while (path[i] && base + i < 254) { p->cwd_path[base + i] = path[i]; i++; }
        p->cwd_path[base + i] = '\0';
    }
    return 0;
}

// ---------------------------------------------------------------------------
// sys_getcwd
// ---------------------------------------------------------------------------
static int64_t sys_getcwd(char *buf, uint64_t n) {
    if (!uptr_ok(buf)) return -EFAULT;
    struct pcb *p = current_proc();
    if (!p) return -EBADF;

    const char *cwd = p->cwd_path[0] ? p->cwd_path : "/";
    int len = 0;
    while (cwd[len]) len++;
    len++; // include null terminator

    if ((uint64_t)len > n) return -ERANGE;
    for (int i = 0; i < len; i++) buf[i] = cwd[i];
    return len - 1;
}

// ---------------------------------------------------------------------------
// sys_getpid
// ---------------------------------------------------------------------------
static int64_t sys_getpid(void) {
    struct pcb *p = current_proc();
    return p ? (int64_t)p->pid : -1;
}

// ---------------------------------------------------------------------------
// sys_exec — replace the current process image with an ELF via VFS
// ---------------------------------------------------------------------------
static int64_t sys_exec(const char *path, uint64_t *trapframe) {
    struct pcb *p = current_proc();
    if (!p || !p->is_user) return -1;
    if (!uptr_ok(path)) return -EFAULT;

    // Resolve through VFS.
    struct inode *ip;
    if (namei(path, &ip) < 0) {
        printk("exec: '%s' not found\n", path);
        return -ENOENT;
    }
    unsigned long img_sz = ip->size;
    // tarfs: direct data pointer via fs_data.
    struct { const char *data; unsigned long file_size; } *td = ip->fs_data;
    const void *img = td->data;
    inode_put(ip);

    if (!img) return -ENOENT;

    pgtable_t new_pt = create_user_pgtable();
    if (!new_pt) return -ENOMEM;

    unsigned long entry;
    if (load_user_elf(new_pt, img, img_sz, &entry) < 0) {
        free_user_pgtable(new_pt);
        return -1;
    }
    if (map_stack(new_pt) < 0) {
        free_user_pgtable(new_pt);
        return -1;
    }

    pgtable_t old_pt = p->pagetable;
    p->pagetable  = new_pt;
    p->user_entry = entry;
    p->user_sp    = USER_STACK_TOP;

    trapframe[TF_SEPC] = entry;
    write_sscratch(USER_STACK_TOP);
    write_satp(make_satp(new_pt));
    flush_tlb();
    free_user_pgtable(old_pt);

    printk("exec: '%s' loaded, entry=0x%lx\n", path, entry);
    return 0;
}

// ---------------------------------------------------------------------------
// sys_fork
// ---------------------------------------------------------------------------
static int64_t sys_fork(void) {
    return (int64_t)proc_fork_current();
}

// ---------------------------------------------------------------------------
// sys_getppid
// ---------------------------------------------------------------------------
static int64_t sys_getppid(void) {
    struct pcb *p = current_proc();
    return p ? (int64_t)p->parent_pid : -1;
}

// ---------------------------------------------------------------------------
// sys_yield
// ---------------------------------------------------------------------------
static int64_t sys_yield(void) {
    yield();
    return 0;
}

// ---------------------------------------------------------------------------
// sys_sleep
// ---------------------------------------------------------------------------
static int64_t sys_sleep(uint64_t ms) {
    proc_sleep_ms(ms);
    return 0;
}

// ---------------------------------------------------------------------------
// syscall_dispatch
// ---------------------------------------------------------------------------
int64_t syscall_dispatch(uint64_t sysnum, uint64_t *trapframe) {
    switch (sysnum) {
        case SYS_exit:
            return sys_exit((int)(int64_t)trapframe[TF_A0]);

        case SYS_write:
            return sys_write((int)(int64_t)trapframe[TF_A0],
                             (const char *)trapframe[TF_A1],
                             trapframe[TF_A2]);

        case SYS_read:
            return sys_read((int)(int64_t)trapframe[TF_A0],
                            (void *)trapframe[TF_A1],
                            trapframe[TF_A2]);

        case SYS_open:
            return sys_open((const char *)trapframe[TF_A0],
                            (int)(int64_t)trapframe[TF_A1]);

        case SYS_close:
            return sys_close((int)(int64_t)trapframe[TF_A0]);

        case SYS_getpid:
            return sys_getpid();

        case SYS_exec:
            return sys_exec((const char *)trapframe[TF_A0], trapframe);

        case SYS_fork:
            return sys_fork();

        case SYS_wait: {
            int *ustatus = (int *)(uintptr_t)trapframe[TF_A0];
            if (ustatus && !uptr_ok(ustatus)) return -1;
            return (int64_t)proc_wait_current(ustatus);
        }

        case SYS_getppid:
            return sys_getppid();

        case SYS_yield:
            return sys_yield();

        case SYS_sleep:
            return sys_sleep(trapframe[TF_A0]);

        case SYS_dup:
            return sys_dup((int)(int64_t)trapframe[TF_A0]);

        case SYS_dup2:
            return sys_dup2((int)(int64_t)trapframe[TF_A0],
                            (int)(int64_t)trapframe[TF_A1]);

        case SYS_lseek:
            return sys_lseek((int)(int64_t)trapframe[TF_A0],
                             (int64_t)trapframe[TF_A1],
                             (int)(int64_t)trapframe[TF_A2]);

        case SYS_fstat:
            return sys_fstat((int)(int64_t)trapframe[TF_A0],
                             (struct stat *)trapframe[TF_A1]);

        case SYS_getdents64:
            return sys_getdents64((int)(int64_t)trapframe[TF_A0],
                                  (void *)trapframe[TF_A1],
                                  trapframe[TF_A2]);

        case SYS_chdir:
            return sys_chdir((const char *)trapframe[TF_A0]);

        case SYS_getcwd:
            return sys_getcwd((char *)trapframe[TF_A0], trapframe[TF_A1]);

        default:
            printk("syscall: unknown number %lu from pid %d\n",
                   sysnum,
                   current_proc() ? current_proc()->pid : -1);
            return -ENOSYS;
    }
}
