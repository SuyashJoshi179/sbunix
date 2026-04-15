// write_test: verify the write() syscall contract.
//
// Tests:
//   1. write to stdout (fd 1) returns the byte count written.
//   2. write to stderr (fd 2) returns the byte count written.
//   3. write to an invalid fd returns -1 (error).
//   4. write of zero bytes returns 0.
//
// Expected output:
//   [write_test] PASS stdout write returned 6
//   [write_test] PASS stderr write returned 6
//   [write_test] PASS bad fd returned -1
//   [write_test] PASS zero-length write returned 0
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    const char *msg = "hello\n";
    long n;

    // 1. stdout
    n = write(1, msg, 6);
    if (n == 6)
        printf("[write_test] PASS stdout write returned %ld\n", n);
    else
        printf("[write_test] FAIL stdout write returned %ld (expected 6)\n", n);

    // 2. stderr
    n = write(2, msg, 6);
    if (n == 6)
        printf("[write_test] PASS stderr write returned %ld\n", n);
    else
        printf("[write_test] FAIL stderr write returned %ld (expected 6)\n", n);

    // 3. invalid fd (fd 42 is not supported) — any negative return is an error
    n = write(42, msg, 6);
    if (n < 0)
        printf("[write_test] PASS bad fd returned error (%ld)\n", n);
    else
        printf("[write_test] FAIL bad fd returned %ld (expected < 0)\n", n);

    // 4. zero-length write to stdout
    n = write(1, msg, 0);
    if (n == 0)
        printf("[write_test] PASS zero-length write returned 0\n");
    else
        printf("[write_test] FAIL zero-length write returned %ld (expected 0)\n", n);

    return 0;
}
