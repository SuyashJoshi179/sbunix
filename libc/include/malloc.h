#ifndef _MALLOC_H
#define _MALLOC_H

#include <stdlib.h>

#define M_TRIM_THRESHOLD    (-1)
#define M_MMAP_THRESHOLD    (-3)

static inline int mallopt(int param, int value) {
    (void)param;
    (void)value;
    return 0;
}

#endif
