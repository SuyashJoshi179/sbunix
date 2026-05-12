#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int parse_int(const char *s, long *out) {
    if (!s || !*s) return -1;
    long v = 0;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') { s++; }
    if (!*s) return -1;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return -1;
        v = v * 10 + (*s - '0');
    }
    *out = neg ? -v : v;
    return 0;
}

static int unary(const char *op, const char *arg) {
    struct stat st;
    if (!strcmp(op, "-e")) return stat(arg, &st) == 0;
    if (!strcmp(op, "-f")) return stat(arg, &st) == 0 && S_ISREG(st.st_mode);
    if (!strcmp(op, "-d")) return stat(arg, &st) == 0 && S_ISDIR(st.st_mode);
    if (!strcmp(op, "-n")) return arg[0] != 0;
    if (!strcmp(op, "-z")) return arg[0] == 0;
    return -1;
}

static int binary(const char *a, const char *op, const char *b) {
    if (!strcmp(op, "=")  || !strcmp(op, "==")) return strcmp(a, b) == 0;
    if (!strcmp(op, "!=")) return strcmp(a, b) != 0;
    long x, y;
    if (parse_int(a, &x) < 0 || parse_int(b, &y) < 0) return -1;
    if (!strcmp(op, "-eq")) return x == y;
    if (!strcmp(op, "-ne")) return x != y;
    if (!strcmp(op, "-lt")) return x <  y;
    if (!strcmp(op, "-gt")) return x >  y;
    if (!strcmp(op, "-le")) return x <= y;
    if (!strcmp(op, "-ge")) return x >= y;
    return -1;
}

int main(int argc, char **argv) {
    /* `[` form: require trailing `]`. */
    const char *prog = argv[0];
    const char *slash = strrchr(prog, '/');
    if (slash) prog = slash + 1;
    if (!strcmp(prog, "[")) {
        if (argc < 2 || strcmp(argv[argc - 1], "]") != 0) {
            fprintf(stderr, "[: missing ]\n");
            return 2;
        }
        argc--;
    }

    /* test            -> false
     * test STR        -> true iff STR non-empty
     * test ! ...      -> negate result of ...
     * test -OP ARG    -> unary
     * test A OP B     -> binary
     */
    if (argc < 2) return 1;

    int negate = 0;
    int i = 1;
    if (!strcmp(argv[i], "!")) { negate = 1; i++; }

    int rc;
    int rem = argc - i;
    if (rem == 1) {
        rc = argv[i][0] != 0;
    } else if (rem == 2) {
        rc = unary(argv[i], argv[i + 1]);
    } else if (rem == 3) {
        rc = binary(argv[i], argv[i + 1], argv[i + 2]);
    } else {
        fprintf(stderr, "test: too many arguments\n");
        return 2;
    }

    if (rc < 0) {
        fprintf(stderr, "test: bad expression\n");
        return 2;
    }
    if (negate) rc = !rc;
    return rc ? 0 : 1;
}
