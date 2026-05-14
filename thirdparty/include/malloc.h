#pragma once
#include <sys/types.h>
#if __has_include_next(<malloc.h>)
# include_next <malloc.h>
#else
# include <stdlib.h>
#endif
