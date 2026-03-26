#include <printk.h>
#include <riscv.h>
#include <sbi.h>
#include <trap.h>
#include <timer.h>


void boot(unsigned long hartid, unsigned long dtb_addr) {
	printk("Booting SBUnix\n");
	printk("Testing pointer %o\n", 123);
	printk("Testing sstatus register: %lx\n", read_scause());
	sbi_set_timer(read_time() + 100000000);
	printk("Initializing traps\n");
	trap_init();
	printk("Initializing timer\n");
	timer_init();
	printk("Waiting for interrupts...\n");
}