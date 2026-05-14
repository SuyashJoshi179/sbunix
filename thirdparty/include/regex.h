#pragma once
#include <sys/types.h>

typedef struct { int __dummy; } regex_t;
typedef size_t regoff_t;
typedef struct { regoff_t rm_so; regoff_t rm_eo; } regmatch_t;

#define REG_EXTENDED 1
#define REG_ICASE    2
#define REG_NOSUB    4
#define REG_NEWLINE  8
#define REG_NOTBOL   1
#define REG_NOTEOL   2
#define REG_NOMATCH  1
