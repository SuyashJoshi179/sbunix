#ifndef CONSOLE_H
#define CONSOLE_H

void console_init();
int console_read(char *dst, int n);
int console_read_polled(char *dst, int n);
void console_putc(char c);
void console_rx_interrupt(void);

#endif