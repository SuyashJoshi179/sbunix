#include <getopt.h>
#include <stdio.h>
#include <string.h>

char *optarg = 0;
int   optind = 1;
int   opterr = 1;
int   optopt = 0;

/* Position within a clustered option group (e.g. "-abc"). Reset whenever
 * we move to a new argv element. */
static int subind = 1;

static void warn_opt(const char *prog, const char *msg, char c) {
    if (!opterr) return;
    if (prog) fprintf(stderr, "%s: ", prog);
    fprintf(stderr, "%s -- '%c'\n", msg, c);
}

int getopt(int argc, char *const argv[], const char *opts) {
    optarg = 0;
    /* glibc convention: optind == 0 means "reset state, skip argv[0]". BB's
     * GETOPT_RESET() relies on this — without it, argv[0] (program name) is
     * treated as a positional and never advanced past. */
    if (optind == 0) { optind = 1; subind = 1; }
    if (optind >= argc)                              { subind = 1; return -1; }
    const char *arg = argv[optind];
    if (!arg || arg[0] != '-' || arg[1] == '\0')    { subind = 1; return -1; }
    if (arg[1] == '-' && arg[2] == '\0')             { subind = 1; optind++; return -1; }

    char c = arg[subind];
    const char *p = strchr(opts, c);
    if (!c || !p || c == ':') {
        optopt = c;
        warn_opt(argv[0], "invalid option", c);
        if (arg[++subind] == '\0') { optind++; subind = 1; }
        return '?';
    }

    if (p[1] == ':') {
        /* option takes an argument */
        if (arg[subind + 1]) {
            optarg = (char *)&arg[subind + 1];
            optind++;
            subind = 1;
        } else if (optind + 1 < argc) {
            optarg = argv[optind + 1];
            optind += 2;
            subind = 1;
        } else {
            optopt = c;
            warn_opt(argv[0], "option requires an argument", c);
            optind++;
            subind = 1;
            return (opts[0] == ':') ? ':' : '?';
        }
    } else {
        if (arg[++subind] == '\0') { optind++; subind = 1; }
    }
    return (int)(unsigned char)c;
}

static int long_match(const char *arg, const struct option *opt, int *eq_pos) {
    /* Match arg against opt->name, allowing a "=" separator. Returns 1 on
     * full match, 0 otherwise. *eq_pos is set to the index of '=' if any. */
    int i = 0;
    while (opt->name[i] && arg[i] && arg[i] != '=' && arg[i] == opt->name[i]) i++;
    if (opt->name[i] != '\0') return 0;
    if (arg[i] != '\0' && arg[i] != '=') return 0;
    *eq_pos = (arg[i] == '=') ? i : -1;
    return 1;
}

int getopt_long(int argc, char *const argv[], const char *opts,
                const struct option *longopts, int *longindex) {
    if (optind == 0) { optind = 1; subind = 1; }
    if (optind >= argc) return -1;
    const char *arg = argv[optind];
    if (!arg || arg[0] != '-') return -1;
    if (arg[1] != '-') return getopt(argc, argv, opts);
    if (arg[2] == '\0') { optind++; return -1; }

    const char *name = arg + 2;
    optarg = 0;
    for (int i = 0; longopts && longopts[i].name; i++) {
        int eq;
        if (!long_match(name, &longopts[i], &eq)) continue;
        if (longindex) *longindex = i;
        optind++;
        if (longopts[i].has_arg == required_argument) {
            if (eq >= 0) optarg = (char *)&name[eq + 1];
            else if (optind < argc) optarg = argv[optind++];
            else { optopt = longopts[i].val; return (opts[0] == ':') ? ':' : '?'; }
        } else if (longopts[i].has_arg == optional_argument && eq >= 0) {
            optarg = (char *)&name[eq + 1];
        }
        if (longopts[i].flag) { *longopts[i].flag = longopts[i].val; return 0; }
        return longopts[i].val;
    }
    optind++;
    return '?';
}
