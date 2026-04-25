#ifndef _STDDEF_H
#define _STDDEF_H

#ifndef NULL
#define NULL ((void *)0)
#endif

#ifndef _SIZE_T_DEFINED
#define _SIZE_T_DEFINED
typedef unsigned long size_t;
#endif

#ifndef _PTRDIFF_T_DEFINED
#define _PTRDIFF_T_DEFINED
typedef long ptrdiff_t;
#endif

#ifndef _WCHAR_T_DEFINED
#define _WCHAR_T_DEFINED
typedef int wchar_t;
#endif

#define offsetof(type, member) __builtin_offsetof(type, member)

#endif
