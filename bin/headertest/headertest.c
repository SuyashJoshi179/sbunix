#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fail_count = 0;

#define CHECK(expr) do {                                              \
    if (!(expr)) { fail_count++;                                      \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); } \
} while (0)

int main(void) {
    /* NULL pointer constant from multiple headers */
    char *p = NULL;
    CHECK(p == NULL);
    CHECK(getenv("PATH") == NULL);

    /* size_t in string ops */
    size_t n = strlen("hello");
    CHECK(n == 5);

    char buf[64];
    int written = snprintf(buf, sizeof(buf), "x=%d y=%s n=%lu", 42, "ok", (unsigned long)n);
    CHECK(written > 0);
    CHECK(strcmp(buf, "x=42 y=ok n=5") == 0);

    /* snprintf truncation */
    char small[8];
    int w2 = snprintf(small, sizeof(small), "abcdefghij");
    CHECK(w2 == 10);
    CHECK(strlen(small) == 7);

    /* fprintf to stdout/stderr */
    fprintf(stdout, "headertest: stdout fprintf ok\n");

    /* atoi / strtol */
    CHECK(atoi("  -123abc") == -123);
    char *end;
    long v = strtol("0x2a", &end, 0);
    CHECK(v == 42);
    CHECK(*end == 0);

    /* abs */
    CHECK(abs(-7) == 7);

    /* puts / fputs */
    fputs("headertest: fputs ok\n", stdout);

    if (fail_count == 0) {
        puts("headertest: PASS");
        return 0;
    }
    fprintf(stderr, "headertest: FAIL (%d errors)\n", fail_count);
    return 1;
}
