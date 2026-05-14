#pragma once
#include_next <sys/types.h>

#ifndef HAVE_CLOCK_T
typedef long clock_t;
#endif
#ifndef HAVE_USECONDS_T
typedef unsigned int useconds_t;
#endif
#ifndef HAVE_SUSECONDS_T
typedef long suseconds_t;
#endif
