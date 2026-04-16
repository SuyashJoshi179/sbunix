#include <drivers/uart.h> 
#include <console.h>
#include <proc.h>

#define INPUT_BUF_SIZE 128
#define BACKSPACE 0x7f    

// TODO: Module needs to be modified to send data to process and wake it up later.

// The line discipline buffer state
static struct {
    char buf[INPUT_BUF_SIZE];
    unsigned int r;  // where user processes read from
    unsigned int w;  // where committed lines end (Enter pressed)
    unsigned int e;  // where current typing/buffering happens
} cons;

// PID of process sleeping in console_read; 0 = nobody waiting
static int cons_blocked_pid = 0;

/* ----------------------------------------------------------------
 * Output with special backspace handling
 * ---------------------------------------------------------------- */
void console_putc(char c) {
    if (c == BACKSPACE || c == '\b') {
        write_char('\b');
        write_char(' ');
        write_char('\b');
    } else {
        write_char(c);
    }
}

/* ----------------------------------------------------------------
 * Line discipline - process one character
 * Called from interrupt context (console_rx_interrupt)
 * ---------------------------------------------------------------- */
void console_intr(char c) {
    switch(c) {
        case BACKSPACE:
        case '\b':
            if (cons.e != cons.w) {
                cons.e--;
                console_putc(BACKSPACE);
            }
            break;
            
        case '\r':
        case '\n':
            console_putc('\r');
            console_putc('\n');
            
            cons.buf[cons.e++ % INPUT_BUF_SIZE] = '\n';
            cons.w = cons.e; // Commit the line

			// Wake any blocked reader
			if (cons_blocked_pid) {
				proc_wakeup(cons_blocked_pid);
				cons_blocked_pid = 0;
			}
            break;
            
        default:
            if (c != 0 && (cons.e - cons.r < INPUT_BUF_SIZE)) {
                console_putc(c); 
                cons.buf[cons.e++ % INPUT_BUF_SIZE] = c;
            }
            break;
    }
}

/* ----------------------------------------------------------------
 * Interrupt handler - called by UART ISR when bytes arrive
 * Drains UART ring buffer and processes through line discipline
 * ---------------------------------------------------------------- */
void console_rx_interrupt(void) {
	int c;
    // Drain all available bytes from UART buffer
    while ((c = uart_getc()) != -1) {
        console_intr((char)c);
    }
}

/* ----------------------------------------------------------------
 * Blocking read - sleep until data is available
 * ---------------------------------------------------------------- */
int console_read(char *dst, int n) {
    int target = n;
    
    while (n > 0) {
        while (cons.r == cons.w) {
			// No committed data - sleep until ISR wakes us
            struct pcb *p = current_proc();
            if (!p) return -1;
            
			cons_blocked_pid = p->pid;
            proc_sleep(p);
			// Resumed by proc_wakeup in console_intr when Enter is pressed
        }

        char c = cons.buf[cons.r++ % INPUT_BUF_SIZE];
        *dst++ = c;
        n--;

        if (c == '\n') {
            break;
        }
    }
    
    return target - n; 
}

/* ----------------------------------------------------------------
 * Polling mode read - for early boot / panic handlers
 * Bypasses interrupt system entirely
 * ---------------------------------------------------------------- */
int console_read_polled(char *dst, int n) {
    int target = n;
    
    while (n > 0) {
        while (cons.r == cons.w) {
            char c = read_char();  // Direct polling
            console_intr(c);
        }

        char c = cons.buf[cons.r++ % INPUT_BUF_SIZE];
        *dst++ = c;
        n--;

        if (c == '\n') {
            break;
        }
    }
    
    return target - n; 
}

void console_init() {
	cons.r = 0;
	cons.w = 0;
	cons.e = 0;
	cons_blocked_pid = 0;

	// Register our interrupt handler with UART
    uart_set_rx_callback(console_rx_interrupt);

	// Enable UART interrupts
    uart_init();
}