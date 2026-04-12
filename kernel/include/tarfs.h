#pragma once
#include <stdint.h>

/* Find a file by name in the embedded tar archive.
   Returns pointer to file data, sets *size to file size.
   Returns 0 if not found. */
char *tarfs_find(const char *name, unsigned long *size);
