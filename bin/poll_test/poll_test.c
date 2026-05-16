#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

/* poll(2) / ppoll(2): libc-side shim over select. Verifies that:
 *   - a read-ready fd (data already in pipe) reports POLLIN
 *   - a write-ready fd (pipe with reader) reports POLLOUT
 *   - timeout=0 returns 0 with all revents cleared when nothing ready
 *   - an invalid fd returns 1 and revents=POLLNVAL on that slot
 *   - fd<0 is skipped silently (revents=0)
 *   - ppoll honors timespec timeout */
int main(void) {
    int p[2];
    if (pipe(p) < 0) { printf("FAIL: pipe errno=%d\n", errno); return 1; }

    /* Write side ready for POLLOUT immediately. */
    struct pollfd pf[4];
    pf[0].fd = p[1]; pf[0].events = POLLOUT; pf[0].revents = 0;
    int r = poll(pf, 1, 0);
    if (r != 1 || !(pf[0].revents & POLLOUT)) {
        printf("FAIL: poll POLLOUT r=%d rev=%x\n", r, pf[0].revents);
        return 1;
    }

    /* Empty pipe → POLLIN times out. */
    pf[0].fd = p[0]; pf[0].events = POLLIN; pf[0].revents = 0;
    r = poll(pf, 1, 0);
    if (r != 0 || pf[0].revents != 0) {
        printf("FAIL: poll empty pipe r=%d rev=%x\n", r, pf[0].revents);
        return 1;
    }

    /* Write then poll → POLLIN. */
    if (write(p[1], "x", 1) != 1) { printf("FAIL: write\n"); return 1; }
    pf[0].fd = p[0]; pf[0].events = POLLIN; pf[0].revents = 0;
    r = poll(pf, 1, 100);
    if (r != 1 || !(pf[0].revents & POLLIN)) {
        printf("FAIL: poll POLLIN r=%d rev=%x\n", r, pf[0].revents);
        return 1;
    }

    /* Invalid fd → POLLNVAL. */
    pf[0].fd = 999; pf[0].events = POLLIN; pf[0].revents = 0;
    r = poll(pf, 1, 0);
    if (r != 1 || pf[0].revents != POLLNVAL) {
        printf("FAIL: poll bad fd r=%d rev=%x\n", r, pf[0].revents);
        return 1;
    }

    /* Negative fd → skipped silently. */
    pf[0].fd = -1; pf[0].events = POLLIN; pf[0].revents = 0xff;
    r = poll(pf, 1, 0);
    if (r != 0 || pf[0].revents != 0) {
        printf("FAIL: poll neg fd r=%d rev=%x\n", r, pf[0].revents);
        return 1;
    }

    /* Mixed: one valid + one invalid → POLLNVAL counted, valid checked. */
    char buf;
    read(p[0], &buf, 1); /* drain */
    pf[0].fd = p[1]; pf[0].events = POLLOUT;
    pf[1].fd = 999;  pf[1].events = POLLIN;
    pf[0].revents = pf[1].revents = 0;
    r = poll(pf, 2, 0);
    if (r < 1 || !(pf[1].revents & POLLNVAL)) {
        printf("FAIL: mixed poll r=%d rev0=%x rev1=%x\n",
               r, pf[0].revents, pf[1].revents);
        return 1;
    }

    /* ppoll: 0-timeout (struct timespec). */
    struct timespec ts = { 0, 0 };
    pf[0].fd = p[0]; pf[0].events = POLLIN; pf[0].revents = 0;
    r = ppoll(pf, 1, &ts, 0);
    if (r != 0) {
        printf("FAIL: ppoll 0-timeout r=%d\n", r);
        return 1;
    }

    close(p[0]); close(p[1]);
    printf("PASS\n");
    return 0;
}
