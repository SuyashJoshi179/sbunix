#include <drivers/uart.h> 
#include <console.h>

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

void console_putc(char c) {
    if (c == BACKSPACE || c == '\b') {
        write_char('\b');
        write_char(' ');
        write_char('\b');
    } else {
        write_char(c);
    }
}

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
            cons.w = cons.e; 
            break;
            
        default:
            if (c != 0 && (cons.e - cons.r < INPUT_BUF_SIZE)) {
                console_putc(c); 
                cons.buf[cons.e++ % INPUT_BUF_SIZE] = c;
            }
            break;
    }
}

int console_read(char *dst, int n) {
    int target = n;
    
    while (n > 0) {
        while (cons.r == cons.w) {
            char c = read_char(); 
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

	// TODO: Add devsw connection
}