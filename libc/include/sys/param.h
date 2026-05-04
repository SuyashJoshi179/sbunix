#ifndef _SYS_PARAM_H
#define _SYS_PARAM_H

#include <limits.h>

#ifndef NBBY
#define NBBY CHAR_BIT
#endif

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifndef howmany
#define howmany(x, y) (((x) + ((y) - 1)) / (y))
#endif

#ifndef roundup
#define roundup(x, y) (howmany((x), (y)) * (y))
#endif

#endif
