#include <drivers/uart.h>

#define UART 0x10000000

#define THR (*(volatile unsigned char *)(UART + 0x00))
#define RBR (*(volatile unsigned char *)(UART + 0x00))
#define LSR (*(volatile unsigned char *)(UART + 0x05))

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