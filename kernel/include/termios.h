#pragma once
#include <stdint.h>

struct termios {
    uint32_t c_iflag;
    uint32_t c_oflag;
    uint32_t c_cflag;
    uint32_t c_lflag;
    uint8_t  c_cc[32];
};

#define ICRNL   0x00000100

#define ONLCR   0x00000004

#define ISIG    0x00000001
#define ICANON  0x00000002
#define ECHO    0x00000008

#define VINTR    0
#define VERASE   2
#define VEOF     4
#define VTIME    5
#define VMIN     6

struct winsize {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
};

#define TCGETS      0x5401
#define TCSETS      0x5402
#define TIOCGWINSZ  0x5413
#define TIOCSPGRP   0x5410
#define TIOCGPGRP   0x540F

void termios_init(void);
void termios_get(struct termios *out);
void termios_set(const struct termios *in);
int termios_get_fg_pid(void);
void termios_set_fg_pid(int pid);
