#include <stdint.h>
#include <riscv.h>
#include <printk.h>
#include <timer.h>
#include <proc.h>
#include <vmem.h>
#include <drivers/uart.h>
#include <syscall.h>

extern void enter_user(unsigned long satp, unsigned long sepc, unsigned long usp, unsigned long ksp);

enum {
    TF_A0 = 9,
    TF_A1 = 10,
    TF_A2 = 11,
    TF_A7 = 16,
};

static long sys_write(uint64_t *trapframe) {
    unsigned long fd = trapframe[TF_A0];
    const char *buf = (const char *)trapframe[TF_A1];
    unsigned long len = trapframe[TF_A2];

    if ((fd != 1 && fd != 2) || buf == 0) {
        return -1;
    }

    for (unsigned long i = 0; i < len; i++) {
        write_char(buf[i]);
    }

    return (long)len;
}

static long sys_read(uint64_t *trapframe) {
    int fd = (int)trapframe[TF_A0];
    char *buf = (char *)trapframe[TF_A1];
    unsigned long len = trapframe[TF_A2];

    if (buf == 0) {
        return -1;
    }

    if (fd == 0) {
        for (unsigned long i = 0; i < len; i++) {
            buf[i] = read_char();
            if (buf[i] == '\n' || buf[i] == '\r') {
                return (long)(i + 1);
            }
        }
        return (long)len;
    }

    return proc_read_current(fd, buf, len);
}

static long handle_syscall(uint64_t *trapframe) {
    switch (trapframe[TF_A7]) {
        case SYS_exit:
            proc_exit_current();
            return 0;
        case SYS_write:
            return sys_write(trapframe);
        case SYS_wait:
            return proc_wait_current();
        case SYS_open:
            return proc_open_current((const char *)trapframe[TF_A0]);
        case SYS_read:
            return sys_read(trapframe);
        case SYS_close:
            return proc_close_current((int)trapframe[TF_A0]);
        default:
            proc_exit_current();
            return 0;
    }
}

void trap_init(void) {

    extern void trap_vector(void);
    write_stvec((uint64_t)trap_vector);
    write_sie(read_sie() | SIE_STIE);
    write_sstatus(read_sstatus() | SSTATUS_SIE | SSTATUS_SUM);
}

void trap_handler(uint64_t scause, uint64_t sepc, uint64_t stval, uint64_t *trapframe) {
    uint64_t satp_before = read_satp();
    uint64_t is_interrupt = scause & (1UL << 63);
    uint64_t cause_code = scause & 0xFF;

    if (is_interrupt) {
        write_satp(make_satp(kernel_pgtable));
        flush_tlb();

        switch (cause_code) {
            case 5:
                timer_handler();
                break;
            default:
                printk("Unknown interrupt: %ld\n", cause_code);
                while (1) {}
                break;
        }

        // Restore the interrupted address space before trap return.
        if (read_satp() != satp_before) {
            write_satp(satp_before);
            flush_tlb();
        }
    } else {
        if (cause_code == 8) {
            if (trapframe[TF_A7] == SYS_exec) {
                long ret = proc_exec_current((const char *)trapframe[TF_A0]);
                trapframe[TF_A0] = (uint64_t)ret;
                if (ret == 0) {
                    pgtable_t pt = proc_current_pagetable();
                    uint64_t user_sp = proc_current_user_sp();
                    uint64_t kstack_top = proc_current_kstack_top();
                    enter_user(make_satp(pt), proc_current_user_entry(), user_sp, kstack_top);
                    while (1) {}
                }
                write_sepc(sepc + 4);
                return;
            }

            long ret = handle_syscall(trapframe);
            trapframe[TF_A0] = (uint64_t)ret;
            write_sepc(sepc + 4);
            return;
        }

        write_satp(make_satp(kernel_pgtable));
        flush_tlb();

        printk("Exception: scause=%lx, sepc=%lx, stval=%lx\n", scause, sepc, stval);
        while (1) {}
    }
}

