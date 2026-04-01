#include <drivers/uart.h>

extern unsigned long mem_offset;

#define THR (*(volatile unsigned char *)(UART + mem_offset + 0x00))
#define RBR (*(volatile unsigned char *)(UART + mem_offset + 0x00))
#define LSR (*(volatile unsigned char *)(UART + mem_offset + 0x05))

#define THRE (LSR & 0x20)
#define DR (LSR & 0x01)

void write_char(char c) {
	while (!THRE);
	THR = c;
}

char read_char() {
	while (!DR);
	return RBR;
}