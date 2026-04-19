#include <stdio.h>
#include <sys/ioctl.h>
#include <termios.h>

static int pass, fail;

static void check(int cond, const char *name) {
    if (cond) { printf("PASS  %s\n", name); pass++; }
    else { printf("FAIL  %s\n", name); fail++; }
}

int main(void) {
    struct termios t;
    struct termios orig;

    int r = ioctl(0, TCGETS, &t);
    check(r == 0, "TCGETS succeeds");
    if (r == 0) {
        orig = t;
        check((t.c_lflag & (ISIG | ICANON | ECHO)) == (ISIG | ICANON | ECHO),
              "default lflag has ISIG|ICANON|ECHO");
    }

    t.c_lflag &= ~ECHO;
    r = ioctl(0, TCSETS, &t);
    check(r == 0, "TCSETS succeeds");

    struct termios t2;
    r = ioctl(0, TCGETS, &t2);
    check(r == 0, "TCGETS after TCSETS succeeds");
    if (r == 0)
        check((t2.c_lflag & ECHO) == 0, "ECHO bit cleared");

    r = ioctl(0, TCSETS, &orig);
    check(r == 0, "restore original termios succeeds");

    struct winsize ws;
    r = ioctl(0, TIOCGWINSZ, &ws);
    check(r == 0, "TIOCGWINSZ succeeds");
    if (r == 0)
        check(ws.ws_row == 24 && ws.ws_col == 80, "winsize is 24x80");

    printf("termios_test: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
