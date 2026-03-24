#ifndef CONSOLE_H
#define CONSOLE_H

void console_intr(char c);
int console_read(char *dst, int n);
void console_init();

#endif