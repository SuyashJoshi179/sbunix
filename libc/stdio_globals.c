#include <stdio.h>

static FILE __stdin = { .fd = 0 };
static FILE __stdout = { .fd = 1 };
static FILE __stderr = { .fd = 2 };

FILE *stdin = &__stdin;
FILE *stdout = &__stdout;
FILE *stderr = &__stderr;
