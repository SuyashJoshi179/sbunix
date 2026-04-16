#ifndef UART_H
#define UART_H

#define UART 0x10000000

void write_char(char c);
char read_char(void);
void uart_init(void);
void uart_rx_isr(void);
int uart_rx_getc(void);
void uart_set_rx_callback(void (*callback)(void));

#endif