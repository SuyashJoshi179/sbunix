#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

/* select(2) regression: a pipe with bytes buffered must report the read
 * end ready immediately; an empty pipe with tv={0,0} must poll once and
 * return 0; tv={0, 50000} must time out without spinning forever. */
int main(void) {
    int p[2];
    if (pipe(p) < 0) { perror("pipe"); return 1; }

    /* Case 1: data buffered → read end ready */
    if (write(p[1], "x", 1) != 1) { perror("write"); return 1; }
    fd_set rfds; FD_ZERO(&rfds); FD_SET(p[0], &rfds);
    struct timeval tv = { 0, 0 };
    int n = select(p[0] + 1, &rfds, 0, 0, &tv);
    if (n != 1 || !FD_ISSET(p[0], &rfds)) {
        printf("FAIL: select on ready pipe returned %d (errno=%d)\n", n, errno);
        return 1;
    }
    char c; if (read(p[0], &c, 1) != 1) { perror("read"); return 1; }

    /* Case 2: empty pipe + poll-once → return 0 */
    FD_ZERO(&rfds); FD_SET(p[0], &rfds);
    tv.tv_sec = 0; tv.tv_usec = 0;
    n = select(p[0] + 1, &rfds, 0, 0, &tv);
    if (n != 0) {
        printf("FAIL: poll on empty pipe returned %d (expected 0)\n", n);
        return 1;
    }

    /* Case 3: empty pipe + 50ms timeout → return 0, no hang */
    FD_ZERO(&rfds); FD_SET(p[0], &rfds);
    tv.tv_sec = 0; tv.tv_usec = 50000;
    n = select(p[0] + 1, &rfds, 0, 0, &tv);
    if (n != 0) {
        printf("FAIL: 50ms select returned %d (expected 0)\n", n);
        return 1;
    }

    close(p[0]); close(p[1]);
    printf("PASS\n");
    return 0;
}
