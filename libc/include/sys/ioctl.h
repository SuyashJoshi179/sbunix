#pragma once
#include <stdint.h>

struct winsize {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
};

#define TCGETS      0x5401
#define TCSETS      0x5402
#define TCSETSW     0x5403
#define TCSETSF     0x5404
#define TIOCGWINSZ  0x5413
#define TIOCSPGRP   0x5410
#define TIOCGPGRP   0x540F

int ioctl(int fd, int cmd, void *arg);
