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

    /* file I/O: log-and-replay scenario */
    const char *path = "/data/htest.txt";

    FILE *fw = fopen(path, "w");
    CHECK(fw != NULL);
    if (fw) {
        const char *line1 = "alpha 100\n";
        size_t w = fwrite(line1, 1, strlen(line1), fw);
        CHECK(w == strlen(line1));
        int rc = fprintf(fw, "%s %d\n", "bravo", 200);
        CHECK(rc == 10);
        CHECK(fclose(fw) == 0);
    }

    /* re-open with "w" must truncate */
    fw = fopen(path, "w");
    CHECK(fw != NULL);
    if (fw) {
        fputs("alpha 100\nbravo 200\n", fw);
        fclose(fw);
    }

    /* read back with fread + fseek/ftell/rewind */
    FILE *fr = fopen(path, "r");
    CHECK(fr != NULL);
    if (fr) {
        char rbuf[64] = {0};
        size_t r = fread(rbuf, 1, sizeof(rbuf) - 1, fr);
        CHECK(r > 0);
        CHECK(strcmp(rbuf, "alpha 100\nbravo 200\n") == 0);

        /* feof should be set since we read past end */
        CHECK(feof(fr));
        clearerr(fr);
        CHECK(!feof(fr));

        /* seek to byte 6 ("100\nbravo 200\n") */
        CHECK(fseek(fr, 6, SEEK_SET) == 0);
        CHECK(ftell(fr) == 6);
        char rbuf2[16] = {0};
        size_t r2 = fread(rbuf2, 1, 3, fr);
        CHECK(r2 == 3);
        CHECK(strncmp(rbuf2, "100", 3) == 0);

        rewind(fr);
        CHECK(ftell(fr) == 0);

        /* fgets line by line */
        char line[64];
        char *got = fgets(line, sizeof(line), fr);
        CHECK(got != NULL);
        CHECK(strcmp(line, "alpha 100\n") == 0);
        got = fgets(line, sizeof(line), fr);
        CHECK(got != NULL);
        CHECK(strcmp(line, "bravo 200\n") == 0);
        got = fgets(line, sizeof(line), fr);
        CHECK(got == NULL);
        CHECK(feof(fr));

        CHECK(fclose(fr) == 0);
    }

    /* append mode */
    FILE *fa = fopen(path, "a");
    CHECK(fa != NULL);
    if (fa) {
        fputs("charlie 300\n", fa);
        fclose(fa);
    }
    fr = fopen(path, "r");
    CHECK(fr != NULL);
    if (fr) {
        char rbuf[128] = {0};
        fread(rbuf, 1, sizeof(rbuf) - 1, fr);
        CHECK(strcmp(rbuf, "alpha 100\nbravo 200\ncharlie 300\n") == 0);
        fclose(fr);
    }

    /* remove cleans up */
    CHECK(remove(path) == 0);
    fr = fopen(path, "r");
    CHECK(fr == NULL);

    /* fopen on nonexistent path returns NULL */
    CHECK(fopen("/data/does_not_exist_xyz", "r") == NULL);

    if (fail_count == 0) {
        puts("headertest: PASS");
        return 0;
    }
    fprintf(stderr, "headertest: FAIL (%d errors)\n", fail_count);
    return 1;
}
