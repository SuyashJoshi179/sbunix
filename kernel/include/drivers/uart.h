#define UART 0x10000000

void write_char(char c);
char read_char(void);

/* RX ring / line-discipline interface (Phase 4d). */
void uart_init(void);              /* enable RDA interrupt            */
void uart_rx_isr(void);            /* called from external-IRQ path   */
int  uart_rx_get(char *out);       /* blocks until a full line is ready;
                                      returns 0 on EOF (Ctrl-D), -1 on error */
