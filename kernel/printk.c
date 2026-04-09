#include <printk.h>
#include <stdarg.h>
#include <stdbool.h>
#include <drivers/uart.h>

void printk(const char *fmt, ...) {
	const char* temp = fmt;
	va_list args;
	va_start(args, fmt);

	while (*temp) {
		if (*temp != '%') {
			write_char(*temp);
		}
		else {
			temp++;

			if (*temp == '%') {
				write_char(*temp);
			}
			else if (*temp == 'c') {
				write_char((char)va_arg(args, int));
			}
			else if (*temp == 'd') {
				int num = va_arg(args, int);
				print_number(num, 10, true);	
			}
			else if (*temp == 'x') {
				int num = va_arg(args, int);
				print_number(num, 16, true);
			}
			else if (*temp == 'l') {
				temp++;
				if (*temp == 'x') {
					long num = va_arg(args, long);
					print_number(num, 16, false);
				}
				else if (*temp == 'd') {
					long num = va_arg(args, long);
					print_number(num, 10, true);
				}
				else if (*temp == 'u') {
					unsigned long num = va_arg(args, unsigned long);
					print_number(num, 10, false);
				}
			}
			else if (*temp == 'o') {
				int num = va_arg(args, int);
				print_number(num, 8, true);
			}
			else if (*temp == 'u') {
				unsigned int num = va_arg(args, unsigned int);
				print_number(num, 10, false);	
			}
			else if (*temp == 'p') {
				unsigned long num = (unsigned long)va_arg(args, void *);
				print_number(num, 16, false);	
			}
			else if (*temp == 's') {
				char* str = va_arg(args, char *);

				while (*str){
					write_char(*str);
					str++;
				}
			}
		}

		temp++;
	}	

	va_end(args);
}

void print_number(long num, int base, int is_signed) {
    char buf[100];
    int i = 0;
    const char *digits = "0123456789abcdef";
    unsigned long unum;

    if (is_signed && num < 0) {
        write_char('-');
        unum = (unsigned long)(-num);
    } 
	else {
        unum = (unsigned long)num;
    }

    do {
        buf[i++] = digits[unum % base];
        unum /= base;
    } while (unum != 0);

    while (i > 0) {
        i--;
        write_char(buf[i]);
    }
}

void panic(const char* msg) {
	printk("KERNEL PANICKING: %s\n", msg);
	for (;;);
}
