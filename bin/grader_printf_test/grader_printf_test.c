/*
 * grader_printf_test — snprintf width, precision, padding, flags.
 *
 * BusyBox uses %05d, %-20s, %8x, %.5s, %o everywhere.
 * A printf that doesn't support width/precision silently corrupts output.
 */
#include <stdio.h>
#include <string.h>

static int fails = 0;
static void check(int cond, const char *name) {
    if (cond) printf("[grader_printf_test] PASS  %s\n", name);
    else { printf("[grader_printf_test] FAIL  %s\n", name); fails++; }
}

int main(void) {
    printf("=== grader_printf_test ===\n");
    char buf[128];

    /* Basic formats (should already work) */
    snprintf(buf, sizeof(buf), "%d", 42);
    check(strcmp(buf, "42") == 0, "%%d basic");

    snprintf(buf, sizeof(buf), "%s", "hello");
    check(strcmp(buf, "hello") == 0, "%%s basic");

    snprintf(buf, sizeof(buf), "%x", 255);
    check(strcmp(buf, "ff") == 0, "%%x basic");

    snprintf(buf, sizeof(buf), "%c", 'A');
    check(strcmp(buf, "A") == 0, "%%c basic");

    snprintf(buf, sizeof(buf), "%%");
    check(strcmp(buf, "%") == 0, "%%%% literal");

    snprintf(buf, sizeof(buf), "%ld", 1234567890L);
    check(strcmp(buf, "1234567890") == 0, "%%ld long");

    snprintf(buf, sizeof(buf), "%p", (void *)0x1234);
    check(strstr(buf, "1234") != 0, "%%p contains address");

    /* Width — right-aligned, space-padded */
    snprintf(buf, sizeof(buf), "%10d", 42);
    check(strlen(buf) == 10, "%%10d is 10 chars wide");
    check(strcmp(buf, "        42") == 0, "%%10d right-aligned");

    snprintf(buf, sizeof(buf), "%10s", "hi");
    check(strlen(buf) == 10, "%%10s is 10 chars wide");
    check(strcmp(buf, "        hi") == 0, "%%10s right-aligned");

    /* Zero-padding */
    snprintf(buf, sizeof(buf), "%05d", 42);
    check(strcmp(buf, "00042") == 0, "%%05d zero-padded");

    snprintf(buf, sizeof(buf), "%08x", 0xabc);
    check(strcmp(buf, "00000abc") == 0, "%%08x zero-padded hex");

    /* Left-align with - flag */
    snprintf(buf, sizeof(buf), "%-10d!", 42);
    check(strcmp(buf, "42        !") == 0, "%%-10d left-aligned");

    snprintf(buf, sizeof(buf), "%-10s!", "hi");
    check(strcmp(buf, "hi        !") == 0, "%%-10s left-aligned");

    /* Precision for strings — truncate */
    snprintf(buf, sizeof(buf), "%.3s", "hello");
    check(strcmp(buf, "hel") == 0, "%%.3s truncates");

    snprintf(buf, sizeof(buf), "%.10s", "hi");
    check(strcmp(buf, "hi") == 0, "%%.10s no pad");

    /* Precision for integers — minimum digits */
    snprintf(buf, sizeof(buf), "%.5d", 42);
    check(strcmp(buf, "00042") == 0, "%%.5d pads with zeros");

    /* Width + precision combined */
    snprintf(buf, sizeof(buf), "%10.5d", 42);
    check(strcmp(buf, "     00042") == 0, "%%10.5d width+precision");

    /* Octal */
    snprintf(buf, sizeof(buf), "%o", 8);
    check(strcmp(buf, "10") == 0, "%%o octal 8");

    snprintf(buf, sizeof(buf), "%o", 255);
    check(strcmp(buf, "377") == 0, "%%o octal 255");

    /* Negative numbers */
    snprintf(buf, sizeof(buf), "%d", -42);
    check(strcmp(buf, "-42") == 0, "%%d negative");

    snprintf(buf, sizeof(buf), "%10d", -42);
    check(strcmp(buf, "       -42") == 0, "%%10d negative right-aligned");

    /* Unsigned */
    snprintf(buf, sizeof(buf), "%u", 4294967295U);
    check(strcmp(buf, "4294967295") == 0, "%%u max uint32");

    /* snprintf truncation: return value is full length */
    int n = snprintf(buf, 5, "hello world");
    check(n == 11, "snprintf returns full length on truncation");
    check(buf[4] == '\0', "snprintf NUL-terminates on truncation");
    check(strncmp(buf, "hell", 4) == 0, "snprintf truncated content correct");

    /* Star width: %*d */
    snprintf(buf, sizeof(buf), "%*d", 8, 42);
    check(strcmp(buf, "      42") == 0, "%%*d star width");

    /* Star precision: %.*s */
    snprintf(buf, sizeof(buf), "%.*s", 3, "hello");
    check(strcmp(buf, "hel") == 0, "%%.*s star precision");

    printf("=== grader_printf_test: %d failures ===\n", fails);
    return fails;
}
