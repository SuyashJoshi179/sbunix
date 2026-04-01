#pragma once

#include <sys/types.h>

struct dirent {
    char d_name[256];
};

typedef struct {
    int fd;
    struct dirent ent;
} DIR;

DIR *opendir(const char *name);
struct dirent *readdir(DIR *dirp);
int closedir(DIR *dirp);
