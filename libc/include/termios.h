#pragma once
#include <stdint.h>
#include <sys/types.h>
#include <sys/ioctl.h>

typedef unsigned int tcflag_t;
typedef unsigned int speed_t;
typedef unsigned char cc_t;

struct termios {
    uint32_t c_iflag;
    uint32_t c_oflag;
    uint32_t c_cflag;
    uint32_t c_lflag;
    uint8_t  c_cc[32];
};

/* iflag */
#define IGNBRK  0x00000001
#define BRKINT  0x00000002
#define IGNPAR  0x00000004
#define PARMRK  0x00000008
#define INPCK   0x00000010
#define ISTRIP  0x00000020
#define INLCR   0x00000040
#define IGNCR   0x00000080
#define ICRNL   0x00000100
#define IUCLC   0x00000200
#define IXON    0x00000400
#define IXANY   0x00000800
#define IXOFF   0x00001000

/* oflag */
#define OPOST   0x00000001
#define ONLCR   0x00000004
#define OCRNL   0x00000008

/* lflag */
#define ISIG    0x00000001
#define ICANON  0x00000002
#define XCASE   0x00000004
#define ECHO    0x00000008
#define ECHOE   0x00000010
#define ECHOK   0x00000020
#define ECHONL  0x00000040
#define NOFLSH  0x00000080
#define TOSTOP  0x00000100
#define IEXTEN  0x00008000

/* cflag baud-rate constants — placeholder values, kernel ignores them */
#define B0      0x00000000
#define B50     0x00000001
#define B75     0x00000002
#define B110    0x00000003
#define B134    0x00000004
#define B150    0x00000005
#define B200    0x00000006
#define B300    0x00000007
#define B600    0x00000008
#define B1200   0x00000009
#define B1800   0x0000000a
#define B2400   0x0000000b
#define B4800   0x0000000c
#define B9600   0x0000000d
#define B19200  0x0000000e
#define B38400  0x0000000f
#define B57600  0x00001001
#define B115200 0x00001002
#define B230400 0x00001003
#define B460800 0x00001004
#define B500000 0x00001005
#define B576000 0x00001006
#define B921600 0x00001007
#define B1000000 0x00001008
#define B1152000 0x00001009
#define B1500000 0x0000100a
#define B2000000 0x0000100b
#define B2500000 0x0000100c
#define B3000000 0x0000100d
#define B3500000 0x0000100e
#define B4000000 0x0000100f
#define CSIZE   0x00000030
#define CS5     0x00000000
#define CS6     0x00000010
#define CS7     0x00000020
#define CS8     0x00000030
#define CSTOPB  0x00000040
#define CREAD   0x00000080
#define PARENB  0x00000100
#define PARODD  0x00000200
#define HUPCL   0x00000400
#define CLOCAL  0x00000800

#define VINTR    0
#define VQUIT    1
#define VERASE   2
#define VKILL    3
#define VEOF     4
#define VTIME    5
#define VMIN     6
#define VSTART   8
#define VSTOP    9
#define VSUSP    10
#define VEOL     11

/* tcsetattr `actions` */
#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2

/* tcflush `queue` */
#define TCIFLUSH  0
#define TCOFLUSH  1
#define TCIOFLUSH 2

/* tcflow `action` */
#define TCOOFF    0
#define TCOON     1
#define TCIOFF    2
#define TCION     3

int     tcgetattr(int fd, struct termios *t);
int     tcsetattr(int fd, int actions, const struct termios *t);
int     tcflush(int fd, int queue);
int     tcdrain(int fd);
int     tcflow(int fd, int action);
int     tcsendbreak(int fd, int duration);
pid_t   tcgetpgrp(int fd);
int     tcsetpgrp(int fd, pid_t pgrp);
pid_t   tcgetsid(int fd);
void    cfmakeraw(struct termios *t);
speed_t cfgetispeed(const struct termios *t);
speed_t cfgetospeed(const struct termios *t);
int     cfsetispeed(struct termios *t, speed_t speed);
int     cfsetospeed(struct termios *t, speed_t speed);
int     cfsetspeed(struct termios *t, speed_t speed);
