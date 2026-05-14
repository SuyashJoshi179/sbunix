#pragma once
/* Defer to a student-provided alloca.h if one exists. */
#if __has_include_next(<alloca.h>)
#include_next <alloca.h>
#endif
#ifndef alloca
#define alloca(size) __builtin_alloca(size)
#endif
