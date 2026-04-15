#include <string.h>

void* memset (void *dest, int c, unsigned int n) {
	char *temp = (char *)dest;

	for (unsigned int i = 0; i < n; i++) {
		temp[i] = c;
	}

	return dest;
}

void* memmove(void* dest, const void* src, unsigned int n) {
	char *cdest = (char *) dest;
	const char *csrc = (char *) src;

	if (cdest > csrc && cdest < csrc + n) {
		for (unsigned int i = n; i > 0; i--) {
			cdest[i - 1] = csrc[i - 1];
		}
	}
	else {
		for (unsigned int i = 0; i < n; i++) {
			cdest[i] = csrc[i];
		}
	}

	return dest;
}

char* strncpy(char *dest, const char* src, unsigned int n) {
	char* temp = dest;
	
	for (unsigned int i = 0; i < n; i++) {
		if (*src) {
			*temp++ = *src++;
		}
		else {
			*temp++ = 0;
		}
	}

	return dest;
}

void* memcpy(void *dest, const void *src, unsigned int n) {
	return memmove(dest, src, n);
}

int strncmp(const char *s1, const char *s2, unsigned int n) {
	for (unsigned int i = 0; i < n; i++) {
		if (s1[i] != s2[i])
			return (unsigned char)s1[i] - (unsigned char)s2[i];
		if (s1[i] == '\0')
			return 0;
	}
	return 0;
}

int strlen(const char *str) {
	int count = 0;
	for (count = 0; str[count]; count++);
	return count;
}

int strcmp(const char *a, const char *b) {
	while (*a && *a == *b) { a++; b++; }
	return (unsigned char)*a - (unsigned char)*b;
}