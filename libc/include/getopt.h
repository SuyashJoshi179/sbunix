#pragma once

extern char *optarg;
extern int   optind;
extern int   opterr;
extern int   optopt;

int getopt(int argc, char *const argv[], const char *opts);

struct option {
    const char *name;
    int         has_arg;
    int        *flag;
    int         val;
};

#define no_argument       0
#define required_argument 1
#define optional_argument 2

int getopt_long(int argc, char *const argv[], const char *opts,
                const struct option *longopts, int *longindex);
