#ifndef _ALLOCA_H
#define _ALLOCA_H

#include <stddef.h>

/* GCC builtin — stack-allocates `size` bytes for current frame, freed
 * automatically on return. */
#define alloca(size) __builtin_alloca(size)

#endif
