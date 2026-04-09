#include <string.h>
#include <stdbool.h>

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

bool strncmp(const char *str1, const char *str2, unsigned int n) {
	for (unsigned int i = 0; i < n; i++) {
		if (str1[i] != str2[i]) {
			return false;
		}
		if (str1[i] == '\0') {
			return true;
		}
	}

	return true;
}

int strlen(const char *str) {
	int count = 0;
	for (count = 0; str[count]; count++);
	return count;
}