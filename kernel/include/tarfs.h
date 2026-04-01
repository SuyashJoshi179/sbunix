#pragma once

#include <stdbool.h>

struct tarfs_node {
    const char *data;
    unsigned long size;
};

void tarfs_init(void);
bool tarfs_lookup(const char *path, struct tarfs_node *out);
unsigned long tarfs_read(const struct tarfs_node *node, unsigned long off, void *dst, unsigned long len);
