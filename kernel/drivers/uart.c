#include <drivers/uart.h>
#include <proc.h>
#include <riscv.h>

extern unsigned long mem_offset;

/* UART register accessors through the kernel upper-half map. */
#define THR (*(volatile unsigned char *)(UART + mem_offset + 0x00))
#define RBR (*(volatile unsigned char *)(UART + mem_offset + 0x00))
#define IER (*(volatile unsigned char *)(UART + mem_offset + 0x01))
#define LSR (*(volatile unsigned char *)(UART + mem_offset + 0x05))

#define THRE (LSR & 0x20)  /* transmit-hold register empty */
#define DR   (LSR & 0x01)  /* data ready */

#define UART_RX_BUF_SIZE 64

static unsigned char uart_rx_buf[UART_RX_BUF_SIZE];
static int uart_rx_head = 0;  /* write index */
static int uart_rx_tail = 0;  /* read index */

static void (*uart_rx_callback)(void) = 0;

/* ----------------------------------------------------------------
 * TX — unchanged from Phase 3
 * ---------------------------------------------------------------- */

void write_char(char c) {
    while (!THRE);
    THR = c;
}

char read_char(void) {
    while (!DR);
    return RBR;
}

/* ----------------------------------------------------------------
 * uart_init — enable the received-data-available interrupt
 * ---------------------------------------------------------------- */

void uart_init(void) {
    IER = 0x01;  /* bit 0 = Received Data Available interrupt enable */
}

int uart_rx_getc(void) {
    if (uart_rx_tail == uart_rx_head)
        return -1;  /* Buffer empty */
    
    unsigned char c = uart_rx_buf[uart_rx_tail];
    uart_rx_tail = (uart_rx_tail + 1) % UART_RX_BUF_SIZE;
    return c;
}

void uart_rx_isr(void) {
    /* Drain all available bytes from hardware FIFO */
    while (DR) {  
        unsigned char c = RBR;  // Read byte from Receive Buffer Register
        
        /* Calculate where next write would go */
        int next_head = (uart_rx_head + 1) % UART_RX_BUF_SIZE;
        
        /* Only store if buffer not full */
        if (next_head != uart_rx_tail) {
            uart_rx_buf[uart_rx_head] = c;  // Store byte
            uart_rx_head = next_head;        // Advance write position
        }
        // If full, byte is dropped
    }
    
    /* Notify console layer */
    if (uart_rx_callback) {
        uart_rx_callback();  // Calls console_rx_interrupt()
    }
}


void uart_set_rx_callback(void (*callback)(void)) {
    uart_rx_callback = callback;
}