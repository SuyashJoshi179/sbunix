#pragma once
#include <stdint.h>
#include <stddef.h>

/* lp64: long is 64-bit, int is 32-bit, short is 16-bit. */

#define PRId8   "d"
#define PRIi8   "i"
#define PRIu8   "u"
#define PRIo8   "o"
#define PRIx8   "x"
#define PRIX8   "X"

#define PRId16  "d"
#define PRIi16  "i"
#define PRIu16  "u"
#define PRIo16  "o"
#define PRIx16  "x"
#define PRIX16  "X"

#define PRId32  "d"
#define PRIi32  "i"
#define PRIu32  "u"
#define PRIo32  "o"
#define PRIx32  "x"
#define PRIX32  "X"

#define PRId64  "ld"
#define PRIi64  "li"
#define PRIu64  "lu"
#define PRIo64  "lo"
#define PRIx64  "lx"
#define PRIX64  "lX"

#define PRIdMAX "ld"
#define PRIiMAX "li"
#define PRIuMAX "lu"
#define PRIoMAX "lo"
#define PRIxMAX "lx"
#define PRIXMAX "lX"

#define PRIdPTR "ld"
#define PRIiPTR "li"
#define PRIuPTR "lu"
#define PRIoPTR "lo"
#define PRIxPTR "lx"
#define PRIXPTR "lX"

#define SCNd8   "hhd"
#define SCNi8   "hhi"
#define SCNu8   "hhu"
#define SCNx8   "hhx"

#define SCNd16  "hd"
#define SCNi16  "hi"
#define SCNu16  "hu"
#define SCNx16  "hx"

#define SCNd32  "d"
#define SCNi32  "i"
#define SCNu32  "u"
#define SCNx32  "x"

#define SCNd64  "ld"
#define SCNi64  "li"
#define SCNu64  "lu"
#define SCNx64  "lx"

#define SCNdMAX "ld"
#define SCNiMAX "li"
#define SCNuMAX "lu"
#define SCNxMAX "lx"

#define SCNdPTR "ld"
#define SCNiPTR "li"
#define SCNuPTR "lu"
#define SCNxPTR "lx"

typedef struct { intmax_t  quot, rem; } imaxdiv_t;

intmax_t  imaxabs(intmax_t  x);
imaxdiv_t imaxdiv(intmax_t  num, intmax_t den);
intmax_t  strtoimax(const char *s, char **endp, int base);
uintmax_t strtoumax(const char *s, char **endp, int base);
