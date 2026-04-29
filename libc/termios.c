#include <termios.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>

/* The kernel only supports TCGETS/TCSETS via ioctl. tcsetattr "actions"
 * (NOW/DRAIN/FLUSH) collapse to TCSETS — the console has no real hardware
 * queue to drain. Higher-level wrappers (tcflush/tcdrain/tcflow) are
 * stubs that succeed without doing anything: enough to satisfy ports
 * that call them defensively. */

int tcgetattr(int fd, struct termios *t) {
    return ioctl(fd, TCGETS, t);
}

int tcsetattr(int fd, int actions, const struct termios *t) {
    (void)actions;
    return ioctl(fd, TCSETS, (void *)t);
}

int tcflush(int fd, int queue)            { (void)fd; (void)queue; return 0; }
int tcdrain(int fd)                       { (void)fd; return 0; }
int tcflow(int fd, int action)            { (void)fd; (void)action; return 0; }
int tcsendbreak(int fd, int duration)     { (void)fd; (void)duration; return 0; }

pid_t tcgetpgrp(int fd) {
    pid_t pgrp = 0;
    if (ioctl(fd, TIOCGPGRP, &pgrp) < 0) return -1;
    return pgrp;
}

int tcsetpgrp(int fd, pid_t pgrp) {
    return ioctl(fd, TIOCSPGRP, &pgrp);
}

void cfmakeraw(struct termios *t) {
    if (!t) return;
    t->c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    t->c_oflag &= ~OPOST;
    t->c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    t->c_cflag &= ~(CSIZE | PARENB);
    t->c_cflag |= CS8;
    t->c_cc[VMIN]  = 1;
    t->c_cc[VTIME] = 0;
}

/* The kernel doesnt model a baud rate; we store/return the value so that
 * ported code which round-trips it still sees its setting. We tuck it
 * into the high bits of c_cflag to keep cfg* round-tripable without
 * disturbing the (also-ignored) line-control bits. */
#define _SPEED_SHIFT 16
#define _SPEED_MASK  (0xFFFFu << _SPEED_SHIFT)

speed_t cfgetispeed(const struct termios *t) {
    return t ? (t->c_cflag & _SPEED_MASK) >> _SPEED_SHIFT : 0;
}

speed_t cfgetospeed(const struct termios *t) {
    return cfgetispeed(t);
}

int cfsetispeed(struct termios *t, speed_t speed) {
    if (!t) return -1;
    t->c_cflag = (t->c_cflag & ~_SPEED_MASK) | ((speed & 0xFFFFu) << _SPEED_SHIFT);
    return 0;
}

int cfsetospeed(struct termios *t, speed_t speed) {
    return cfsetispeed(t, speed);
}

int cfsetspeed(struct termios *t, speed_t speed) {
    return cfsetispeed(t, speed);
}
