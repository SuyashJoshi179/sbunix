// SBUnix HAL: stdout/stdin via fd 1/0; ticks/delay are stubs.

#include <unistd.h>
#include "py/mpconfig.h"
#include "py/mphal.h"

mp_uint_t mp_hal_stdout_tx_strn(const char *str, mp_uint_t len) {
    long off = 0;
    while (off < (long)len) {
        long r = write(STDOUT_FILENO, str + off, len - off);
        if (r <= 0) {
            break;
        }
        off += r;
    }
    return off;
}

int mp_hal_stdin_rx_chr(void) {
    unsigned char c = 0;
    long r = read(STDIN_FILENO, &c, 1);
    if (r <= 0) {
        return -1;
    }
    return c;
}

void mp_hal_delay_ms(mp_uint_t ms) {
    (void)ms;
}

void mp_hal_delay_us(mp_uint_t us) {
    (void)us;
}
