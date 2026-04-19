#include <termios.h>

static struct termios console_tio;
static int console_fg_pid;

void termios_init(void) {
    for (int i = 0; i < 32; i++)
        console_tio.c_cc[i] = 0;

    console_tio.c_iflag = ICRNL;
    console_tio.c_oflag = ONLCR;
    console_tio.c_cflag = 0;
    console_tio.c_lflag = ISIG | ICANON | ECHO;

    console_tio.c_cc[VINTR] = 0x03;
    console_tio.c_cc[VEOF] = 0x04;
    console_tio.c_cc[VERASE] = 0x7f;
    console_tio.c_cc[VMIN] = 1;
    console_tio.c_cc[VTIME] = 0;

    console_fg_pid = 0;
}

void termios_get(struct termios *out) {
    if (!out) return;
    *out = console_tio;
}

void termios_set(const struct termios *in) {
    if (!in) return;
    console_tio = *in;
}

int termios_get_fg_pid(void) {
    return console_fg_pid;
}

void termios_set_fg_pid(int pid) {
    console_fg_pid = pid;
}
