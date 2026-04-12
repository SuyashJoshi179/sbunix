#include <printk.h>
#include <pmem.h>
#include <vmem.h>
#include <riscv.h>
#include <sbi.h>
#include <trap.h>
#include <timer.h>
#include <proc.h>
#include <virtio_blk.h>
#include <fs.h>
#include <file.h>
#include <syscall.h>

#define VIRTIO0 0x10008000

extern char _kernel_end[];
extern void vmem_switch_to_high(unsigned long satp, unsigned long offset);

/* forward declarations for syscall handlers */
extern int64_t sys_open(uint64_t path_uaddr, int flags);
extern int64_t sys_read(int fd, uint64_t buf_uaddr, int n);
extern int64_t sys_write(int fd, uint64_t buf_uaddr, int n);
extern int64_t sys_close(int fd);
extern int64_t sys_ls(uint64_t path_uaddr);

/* ------------------------------------------------------------------ */
/* Layer 5 test: runs as a kernel thread after scheduling starts.     */
/* pagetable==0 so copy_from/to_user treats addresses as kernel-virt. */
/* ------------------------------------------------------------------ */
void syscall_test_thread(void) {
    printk("[syscall_test] starting\n");

    /* Test sys_write to stdout (fd=1) → should appear on console */
    char hello[] = "sys_write → stdout: Hello from Layer 5!\n";
    int w = (int)sys_write(STDOUT_FILENO, (uint64_t)hello, sizeof(hello) - 1);
    printk("[syscall_test] sys_write(stdout) returned %d\n", w);

    /* Test sys_open (O_CREATE), sys_write to file, sys_read from file */
    char path[] = "/layer5test";
    int fd = (int)sys_open((uint64_t)path, O_CREATE | O_RDWR);
    if (fd < 0) {
        printk("[syscall_test] sys_open FAILED\n");
        return;
    }
    printk("[syscall_test] sys_open fd=%d\n", fd);

    char wdata[] = "data written via sys_write";
    w = (int)sys_write(fd, (uint64_t)wdata, sizeof(wdata));
    printk("[syscall_test] sys_write(fd=%d) returned %d\n", fd, w);

    /* rewind by directly resetting offset (no lseek yet) */
    get_current()->ofile[fd]->off = 0;

    char rbuf[64] = {0};
    int r = (int)sys_read(fd, (uint64_t)rbuf, sizeof(wdata));
    printk("[syscall_test] sys_read returned %d, data=\"%s\"\n", r, rbuf);

    sys_close(fd);
    printk("[syscall_test] sys_close done — Layer 5 OK\n");
}

void boot(unsigned long hartid, unsigned long dtb_addr) {
    printk("Booting SBUnix\n");

    pmem_init(_kernel_end, (void *)PHYMEM_END);
    vmem_init();

    vmem_switch_to_high(make_satp(kernel_pgtable), KVMEM_OFFSET);
    vmem_init_post();

    trap_init();
    timer_init();

    virtio_blk_init(KVMEM_OFFSET + VIRTIO0);
    binit();
    fs_init();
    fileinit();

    printk("Starting scheduler\n");
    sched_init();   /* never returns */
}
