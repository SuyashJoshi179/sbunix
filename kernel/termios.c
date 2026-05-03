#include <termios.h>

static struct termios console_tio;
static int console_fg_pgid;
static int console_session_sid;

void termios_init(void) {
    for (int i = 0; i < 32; i++)
        console_tio.c_cc[i] = 0;

    console_tio.c_iflag = ICRNL;
    console_tio.c_oflag = ONLCR;
    console_tio.c_cflag = 0;
    console_tio.c_lflag = ISIG | ICANON | ECHO;

    console_tio.c_cc[VINTR]  = 0x03;
    console_tio.c_cc[VQUIT]  = 0x1C;
    console_tio.c_cc[VERASE] = 0x7f;
    console_tio.c_cc[VEOF]   = 0x04;
    console_tio.c_cc[VSUSP]  = 0x1A;
    console_tio.c_cc[VMIN]   = 1;
    console_tio.c_cc[VTIME]  = 0;

    console_fg_pgid = 0;
    console_session_sid = 0;
}

void termios_get(struct termios *out) {
    if (!out) return;
    *out = console_tio;
}

void termios_set(const struct termios *in) {
    if (!in) return;
    console_tio = *in;
}

int  termios_get_fg_pgid(void)        { return console_fg_pgid; }
void termios_set_fg_pgid(int pgid)    { console_fg_pgid = pgid; }
int  termios_get_session(void)        { return console_session_sid; }
void termios_set_session(int sid)     { console_session_sid = sid; }
