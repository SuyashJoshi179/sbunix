/*
 * POSIX conformance test: <termios.h>
 *
 * Reference: docs/susv5-html/basedefs/termios.h.html
 *
 * Audited POSIX functions: tcgetattr, tcsetattr, tcsendbreak, tcdrain,
 *                          tcflush, tcflow, cfgetispeed, cfgetospeed,
 *                          cfsetispeed, cfsetospeed
 * Required typedefs: cc_t, speed_t, tcflag_t
 * Required struct termios: c_iflag, c_oflag, c_cflag, c_lflag, c_cc[]
 *
 * tcgetpgrp / tcsetpgrp are in <unistd.h> per POSIX; pin in unistd test
 * (currently excluded as not in our libc unistd).
 *
 * Excluded (non-POSIX): cfmakeraw, cfsetspeed (BSD/glibc extension);
 *                       IUCLC, XCASE (Linux legacy flags)
 * Excluded (not implemented): tcgetsid, tcgetwinsize, tcsetwinsize
 */
#include <termios.h>

#define PIN __attribute__((unused)) static

PIN int     (*_pin_tcgetattr)(int, struct termios *) = tcgetattr;
PIN int     (*_pin_tcsetattr)(int, int, const struct termios *) = tcsetattr;
PIN int     (*_pin_tcsendbreak)(int, int) = tcsendbreak;
PIN int     (*_pin_tcdrain)(int) = tcdrain;
PIN int     (*_pin_tcflush)(int, int) = tcflush;
PIN int     (*_pin_tcflow)(int, int) = tcflow;
PIN speed_t (*_pin_cfgetispeed)(const struct termios *) = cfgetispeed;
PIN speed_t (*_pin_cfgetospeed)(const struct termios *) = cfgetospeed;
PIN int     (*_pin_cfsetispeed)(struct termios *, speed_t) = cfsetispeed;
PIN int     (*_pin_cfsetospeed)(struct termios *, speed_t) = cfsetospeed;

__attribute__((unused))
static void _struct_fields(void) {
    struct termios t;
    __builtin_memset(&t, 0, sizeof t);
    (void)t.c_iflag; (void)t.c_oflag; (void)t.c_cflag; (void)t.c_lflag;
    (void)t.c_cc[0];
}

__attribute__((unused))
static void _macro_checks(void) {
    int x = 0;
    /* iflag */
    x |= IGNBRK | BRKINT | IGNPAR | PARMRK | INPCK | ISTRIP
       | INLCR | IGNCR | ICRNL | IXON | IXANY | IXOFF;
    /* oflag */
    x |= OPOST | ONLCR | OCRNL;
    /* lflag */
    x |= ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHONL
       | NOFLSH | TOSTOP | IEXTEN;
    /* baud */
    x |= B0 | B50 | B75 | B110 | B134 | B150 | B200 | B300 | B600
       | B1200 | B1800 | B2400 | B4800 | B9600 | B19200 | B38400;
    (void)x;
}
