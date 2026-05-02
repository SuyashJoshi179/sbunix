#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int int_cmp(const void *a, const void *b) {
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

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
    fprintf(stdout, "header_test: stdout fprintf ok\n");

    /* atoi / strtol */
    CHECK(atoi("  -123abc") == -123);
    char *end;
    long v = strtol("0x2a", &end, 0);
    CHECK(v == 42);
    CHECK(*end == 0);

    /* abs */
    CHECK(abs(-7) == 7);

    /* puts / fputs */
    fputs("header_test: fputs ok\n", stdout);

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

    /* rename: write src, rename to dst, verify old gone + dst content intact */
    {
        const char *src = "/data/ren_a.txt";
        const char *dst = "/data/ren_b.txt";
        FILE *fp = fopen(src, "w");
        CHECK(fp != NULL);
        if (fp) { fputs("renamed payload\n", fp); fclose(fp); }

        CHECK(rename(src, dst) == 0);
        CHECK(fopen(src, "r") == NULL);

        fp = fopen(dst, "r");
        CHECK(fp != NULL);
        if (fp) {
            char rbuf[64] = {0};
            fread(rbuf, 1, sizeof(rbuf) - 1, fp);
            CHECK(strcmp(rbuf, "renamed payload\n") == 0);
            fclose(fp);
        }

        /* rename onto existing target overwrites */
        fp = fopen(src, "w"); fputs("AAAA", fp); fclose(fp);
        CHECK(rename(src, dst) == 0);
        fp = fopen(dst, "r");
        CHECK(fp != NULL);
        if (fp) {
            char rbuf[16] = {0};
            fread(rbuf, 1, sizeof(rbuf) - 1, fp);
            CHECK(strcmp(rbuf, "AAAA") == 0);
            fclose(fp);
        }

        /* rename of nonexistent source fails (libc returns -errno) */
        CHECK(rename("/data/no_such.x", dst) < 0);

        remove(dst);
    }

    /* qsort/bsearch real-world: write numbers to file, read back, sort, search */
    {
        const char *npath = "/data/nums.txt";
        FILE *fp = fopen(npath, "w");
        CHECK(fp != NULL);
        if (fp) {
            int seed[] = { 42, 7, 99, 13, 4, 88, 1, 56, 23, 71 };
            for (size_t i = 0; i < sizeof(seed)/sizeof(seed[0]); i++)
                fprintf(fp, "%d\n", seed[i]);
            fclose(fp);
        }

        fp = fopen(npath, "r");
        CHECK(fp != NULL);
        int nums[16];
        size_t count = 0;
        if (fp) {
            char ln[32];
            while (fgets(ln, sizeof(ln), fp) && count < 16)
                nums[count++] = atoi(ln);
            fclose(fp);
        }
        CHECK(count == 10);

        qsort(nums, count, sizeof(int), int_cmp);
        for (size_t i = 1; i < count; i++)
            CHECK(nums[i - 1] <= nums[i]);
        CHECK(nums[0] == 1);
        CHECK(nums[count - 1] == 99);

        int key = 56;
        int *hit = bsearch(&key, nums, count, sizeof(int), int_cmp);
        CHECK(hit != NULL);
        CHECK(hit && *hit == 56);

        int miss = 1000;
        CHECK(bsearch(&miss, nums, count, sizeof(int), int_cmp) == NULL);

        remove(npath);
    }

    if (fail_count == 0) {
        puts("header_test: PASS");
        return 0;
    }
    fprintf(stderr, "header_test: FAIL (%d errors)\n", fail_count);
    return 1;
}
