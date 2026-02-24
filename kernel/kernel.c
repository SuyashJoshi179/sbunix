#include <printk.h>

void boot(unsigned long hartid, unsigned long dtb_addr) {
	printk("Booting SBUnix\n");
	printk("Testing pointer %o\n", 123);
}