#include <stdbool.h>

void* memset(void *dest, int c, unsigned int n);
void* memmove(void *dest, const void* src, unsigned int n);
char* strncpy(char *dest, const char* src, unsigned int n);
bool strncmp(const char *str1, const char *str2, unsigned int n);
int strlen(const char *str);