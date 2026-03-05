#include <stdarg.h>
#include <stdint.h>
#include "printk.h"

#define UART0 0x10000000UL

static void uart_putc(char c) {
    volatile char *uart = (char *)UART0;
    *uart = c;
}

void printk(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const char *p = fmt;

    while (*p) {
        if (*p == '%') {
            p++;
            if (*p == 'x') {
                unsigned int val = va_arg(args, unsigned int);
                for (int i = 28; i >= 0; i -= 4) {
                    int digit = (val >> i) & 0xF;
                    uart_putc(digit < 10 ? '0'+digit : 'a'+digit-10);
                }
            }
            // add more format specifiers as needed
        } else {
            uart_putc(*p);
        }
        p++;
    }

    va_end(args);
}