#include <printk.h>
#include <pmem.h>

extern char _kernel_end[];

void boot(unsigned long hartid, unsigned long dtb_addr) {
	printk("Booting SBUnix\n");

	pmem_init(_kernel_end, (void *)0x88000000UL);
}