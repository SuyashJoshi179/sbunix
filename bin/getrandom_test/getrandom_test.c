#include <sys/random.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

/* getentropy / getrandom: libc shim seeds xorshift64 from CLOCK_MONOTONIC
 * nanoseconds + stack-address jitter + pid mix. Not cryptographic, but
 * each call must yield non-identical bytes and respect the POSIX size cap. */
int main(void) {
    unsigned char a[32], b[32];
    memset(a, 0, sizeof(a));
    memset(b, 0, sizeof(b));

    /* Two consecutive getentropy calls should not return identical buffers. */
    if (getentropy(a, sizeof(a)) != 0) {
        printf("FAIL: getentropy a errno=%d\n", errno);
        return 1;
    }
    if (getentropy(b, sizeof(b)) != 0) {
        printf("FAIL: getentropy b errno=%d\n", errno);
        return 1;
    }
    if (memcmp(a, b, sizeof(a)) == 0) {
        printf("FAIL: two getentropy calls returned identical bytes\n");
        return 1;
    }

    /* POSIX cap: > 256 bytes must fail with EIO. */
    unsigned char big[300];
    errno = 0;
    if (getentropy(big, sizeof(big)) == 0 || errno != EIO) {
        printf("FAIL: getentropy(300) want EIO got errno=%d\n", errno);
        return 1;
    }

    /* NULL buf with non-zero length → EFAULT. */
    errno = 0;
    if (getentropy(0, 16) == 0 || errno != EFAULT) {
        printf("FAIL: getentropy(NULL,16) want EFAULT got errno=%d\n", errno);
        return 1;
    }

    /* getrandom: no 256-byte cap. Ask for 1024 bytes and verify the buffer
     * is not all zero (vanishingly unlikely under any PRNG). */
    unsigned char big_ok[1024];
    memset(big_ok, 0, sizeof(big_ok));
    ssize_t n = getrandom(big_ok, sizeof(big_ok), 0);
    if (n != (ssize_t)sizeof(big_ok)) {
        printf("FAIL: getrandom(1024) returned %ld errno=%d\n",
               (long)n, errno);
        return 1;
    }
    int allzero = 1;
    for (size_t i = 0; i < sizeof(big_ok); i++)
        if (big_ok[i]) { allzero = 0; break; }
    if (allzero) {
        printf("FAIL: getrandom(1024) returned all-zero buffer\n");
        return 1;
    }

    /* getentropy(buf, 0) is a no-op success. */
    if (getentropy(a, 0) != 0) {
        printf("FAIL: getentropy(buf,0) errno=%d\n", errno);
        return 1;
    }

    printf("PASS\n");
    return 0;
}
