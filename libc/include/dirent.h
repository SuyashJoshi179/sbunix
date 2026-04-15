#pragma once
#include <stdint.h>

struct dirent64 {
    uint64_t d_ino;
    uint64_t d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    char     d_name[];
};

#define DT_UNKNOWN  0
#define DT_CHR      2
#define DT_DIR      4
#define DT_REG      8
