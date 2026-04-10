#include <syscall.h>
#include <proc.h>
#include <printk.h>
#include <stdint.h>
#include <vmem.h>
#include <drivers/uart.h>

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
// syscall_dispatch — called from trap_handler when scause == 8 (U-mode ecall)
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

        default:
            printk("syscall: unknown number %lu from pid %d\n",
                   sysnum,
                   current_proc() ? current_proc()->pid : -1);
            return -1;
    }
}
