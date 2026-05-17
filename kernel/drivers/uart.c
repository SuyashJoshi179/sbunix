#include <drivers/uart.h>
#include <errno.h>
#include <proc.h>
#include <riscv.h>
#include <signal.h>
#include <termios.h>

extern unsigned long mem_offset;

/* UART register accessors through the kernel upper-half map. */
#define THR (*(volatile unsigned char *)(UART + mem_offset + 0x00))
#define RBR (*(volatile unsigned char *)(UART + mem_offset + 0x00))
#define IER (*(volatile unsigned char *)(UART + mem_offset + 0x01))
#define LSR (*(volatile unsigned char *)(UART + mem_offset + 0x05))

#define THRE (LSR & 0x20)  /* transmit-hold register empty */
#define DR   (LSR & 0x01)  /* data ready */

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

/* ================================================================
 * RX ring + canonical line discipline
 * ================================================================
 *
 * Two-layer design:
 *   edit_buf[EDIT_SZ]  — characters typed on the current line, not yet committed
 *   line_buf[LINE_SZ]  — full committed lines waiting to be read
 *
 * The reader blocks (via proc_sleep) until at least one committed line exists.
 * The ISR wakes the blocked reader after committing a line.
 * ================================================================ */

#define EDIT_SZ  256
#define LINE_SZ  512

static char     edit_buf[EDIT_SZ];
static int      edit_len = 0;

static char     line_buf[LINE_SZ];
static int      line_head = 0;   /* read index  */
static int      line_tail = 0;   /* write index */
static int      line_avail = 0;  /* bytes available to read */

/* PID of process sleeping in uart_rx_get; 0 = nobody waiting. */
static int      rx_blocked_pid = 0;

/* Append n bytes from src to the committed-line ring buffer. */
static void line_push(const char *src, int n) {
    for (int i = 0; i < n; i++) {
        line_buf[line_tail] = src[i];
        line_tail = (line_tail + 1) % LINE_SZ;
        line_avail++;
    }
}

/* Commit the current editing line and wake any blocked reader. */
static void line_commit(void) {
    /* Append current line + newline to the ring. */
    if (edit_len > 0)
        line_push(edit_buf, edit_len);
    char nl = '\n';
    line_push(&nl, 1);
    edit_len = 0;

    /* Wake the blocked reader if there is one. */
    if (rx_blocked_pid) {
        proc_wakeup(rx_blocked_pid);
        rx_blocked_pid = 0;
    }
}

/* Commit an EOF marker (empty line, 0 bytes) and wake blocked reader. */
static void line_commit_eof(void) {
    /* Push a NUL byte as the EOF sentinel — uart_rx_get returns 0 on it. */
    char z = '\0';
    line_push(&z, 1);
    edit_len = 0;

    if (rx_blocked_pid) {
        proc_wakeup(rx_blocked_pid);
        rx_blocked_pid = 0;
    }
}

/* ----------------------------------------------------------------
 * uart_rx_isr — called from trap_handler on UART external interrupt
 * ---------------------------------------------------------------- */

void uart_rx_isr(void) {
    /* Drain all available bytes from the UART FIFO. */
    while (DR) {
        char c = RBR;

        struct termios tio;
        termios_get(&tio);

        if (tio.c_lflag & ISIG) {
            int sigchar = 0;
            char ech = 0;
            if (c == (char)tio.c_cc[VINTR])      { sigchar = SIGINT;  ech = 'C'; }
            else if (c == (char)tio.c_cc[VQUIT]) { sigchar = SIGQUIT; ech = '\\'; }
            else if (c == (char)tio.c_cc[VSUSP]) { sigchar = SIGTSTP; ech = 'Z'; }
            if (sigchar) {
                edit_len = 0;
                if (tio.c_lflag & ECHO) {
                    write_char('^');
                    write_char(ech);
                    write_char('\r');
                    write_char('\n');
                }
                int pgid = termios_get_fg_pgid();
                if (pgid > 0)
                    send_signal_pgrp(pgid, sigchar);
                continue;
            }
        }

        if ((tio.c_iflag & ICRNL) && c == '\r')
            c = '\n';

        if (tio.c_lflag & ICANON) {
            if (c == (char)tio.c_cc[VEOF]) {
                /* POSIX VEOF: discard the EOF byte and deliver any buffered
                 * bytes without waiting for a newline. We always push the
                 * EOF sentinel after the buffered bytes; the reader returns
                 * the byte count on mid-line VEOF (i > 0) or zero on
                 * empty-line VEOF, which is what POSIX read() requires. */
                if (edit_len > 0) {
                    line_push(edit_buf, edit_len);
                    edit_len = 0;
                }
                line_commit_eof();
            } else if (c == '\n') {
                if (tio.c_lflag & ECHO) {
                    write_char('\r');
                    write_char('\n');
                }
                line_commit();
            } else if (c == (char)tio.c_cc[VERASE] || c == '\b') {
                if (edit_len > 0) {
                    edit_len--;
                    if (tio.c_lflag & ECHO) {
                        write_char('\b');
                        write_char(' ');
                        write_char('\b');
                    }
                }
            } else if (c >= 0x20 && edit_len < EDIT_SZ - 1) {
                edit_buf[edit_len++] = c;
                if (tio.c_lflag & ECHO)
                    write_char(c);
            }
        } else {
            line_push(&c, 1);
            if (tio.c_lflag & ECHO)
                write_char(c);
            if (rx_blocked_pid) {
                proc_wakeup(rx_blocked_pid);
                rx_blocked_pid = 0;
            }
        }
    }
}

/* ----------------------------------------------------------------
 * uart_rx_get — blocking get of one byte from the committed-line ring
 *
 * Returns:
 *   1              — byte written into *out (normal)
 *   0              — EOF (Ctrl-D sentinel received)
 *  -1              — no current process / generic error
 *  -ERESTARTSYS    — interrupted by an actionable signal; caller (the
 *                    U-mode ecall trap path) must translate this to
 *                    -EINTR or replay the syscall per SA_RESTART
 * ---------------------------------------------------------------- */

int uart_rx_get(char *out) {
    while (line_avail == 0) {
        /* Nothing ready — sleep until the ISR commits a line. */
        struct pcb *p = current_proc();
        if (!p) return -1;
        rx_blocked_pid = p->pid;
        proc_sleep(p);
        /* Resumed by proc_wakeup in uart_rx_isr / line_commit. */
        if (sig_has_actionable(p))
            return -ERESTARTSYS;
    }

    char c = line_buf[line_head];
    line_head = (line_head + 1) % LINE_SZ;
    line_avail--;

    if (c == '\0') return 0;    /* EOF sentinel */
    *out = c;
    return 1;
}
