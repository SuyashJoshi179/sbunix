#include <exec.h>
#include <printk.h>
#include <proc.h>
#include <riscv.h>
#include <stdint.h>
#include <syscall.h>
#include <tarfs.h>
#include <drivers/uart.h>
#include <vmem.h>

// ---------------------------------------------------------------------------
// sys_exit — terminate current process with the given status
// ---------------------------------------------------------------------------
static int64_t sys_exit(int status) {
    proc_exit_current(status);
    return 0;  // unreachable; proc_exit_current never returns
}

// ---------------------------------------------------------------------------
// sys_write — write len bytes from user buf to file descriptor fd
//
// Only fd 1 (stdout) and fd 2 (stderr) are supported for now.
// The buf pointer is a user virtual address; it is accessible because:
//   • the user page table is still active when the syscall runs (RISC-V does
//     not switch SATP on trap entry), and
//   • sstatus.SUM=1 (set in trap_init) lets S-mode touch U-mode pages.
// ---------------------------------------------------------------------------
static int64_t sys_write(int fd, const char *buf, uint64_t len) {
    if (fd != 1 && fd != 2)
        return -1;
    // Basic sanity: reject obviously invalid pointers (kernel-space addresses)
    if ((unsigned long)buf >= KVMEM_OFFSET)
        return -1;
    for (uint64_t i = 0; i < len; i++)
        write_char(buf[i]);
    return (int64_t)len;
}

// ---------------------------------------------------------------------------
// sys_getpid — return the calling process's PID
// ---------------------------------------------------------------------------
static int64_t sys_getpid(void) {
    struct pcb *p = current_proc();
    return p ? (int64_t)p->pid : -1;
}

// ---------------------------------------------------------------------------
// sys_exec — replace the current process image with an ELF from tarfs
//
// a0 = path (user virtual address of null-terminated string)
// On success the function does not return to the old user image; instead
// trapframe[TF_SEPC] is set to the ELF entry and sscratch is updated so
// trap.S sret jumps to the new program with a fresh user stack.
// ---------------------------------------------------------------------------
static int64_t sys_exec(const char *path, uint64_t *trapframe) {
    struct pcb *p = current_proc();
    if (!p || !p->is_user) return -1;

    // path is a user virtual address; accessible because SUM=1 in sstatus
    if ((unsigned long)path >= KVMEM_OFFSET) return -1;

    unsigned long img_size;
    const void *img = tarfs_find(path, &img_size);
    if (!img) {
        printk("exec: '%s' not found\n", path);
        return -1;
    }

    pgtable_t new_pt = create_user_pgtable();
    if (!new_pt) return -1;

    unsigned long entry;
    if (load_user_elf(new_pt, img, img_size, &entry) < 0) {
        free_user_pgtable(new_pt);
        return -1;
    }

    if (map_stack(new_pt) < 0) {
        free_user_pgtable(new_pt);
        return -1;
    }

    pgtable_t old_pt   = p->pagetable;
    p->pagetable       = new_pt;
    p->user_entry      = entry;
    p->user_sp         = USER_STACK_TOP;

    // Redirect sret to the new program's entry point
    trapframe[TF_SEPC] = entry;

    // Update sscratch so trap.S's sp<->sscratch swap gives the user
    // the fresh stack top rather than the old user sp.
    write_sscratch(USER_STACK_TOP);

    // Switch to the new page table now; kernel code runs at physical
    // addresses (VPN[2]=2) which are copied into every user page table.
    write_satp(make_satp(new_pt));
    flush_tlb();

    // Free old address space after switching away from it
    free_user_pgtable(old_pt);

    printk("exec: '%s' loaded, entry=0x%lx\n", path, entry);
    return 0;
}

// ---------------------------------------------------------------------------
// sys_fork — duplicate the current process
// ---------------------------------------------------------------------------
static int64_t sys_fork(void) {
    return (int64_t)proc_fork_current();
}

// ---------------------------------------------------------------------------
// syscall_dispatch — called from trap_handler when scause == 8 (U-mode ecall)
//
// trapframe is the pointer to the saved register file on the kernel stack
// (288-byte frame, laid out as described in syscall.h).
// The return value is written into trapframe[TF_A0] by the caller.
// ---------------------------------------------------------------------------
int64_t syscall_dispatch(uint64_t sysnum, uint64_t *trapframe) {
    switch (sysnum) {
        case SYS_exit:
            return sys_exit((int)(int64_t)trapframe[TF_A0]);

        case SYS_write:
            return sys_write((int)(int64_t)trapframe[TF_A0],
                             (const char *)trapframe[TF_A1],
                             trapframe[TF_A2]);

        case SYS_getpid:
            return sys_getpid();

        case SYS_exec:
            return sys_exec((const char *)trapframe[TF_A0], trapframe);

        case SYS_fork:
            return sys_fork();

        default:
            printk("syscall: unknown number %lu from pid %d\n",
                   sysnum,
                   current_proc() ? current_proc()->pid : -1);
            return -1;
    }
}
