#include <unistd.h>

static char prefix[] = "hello";
static char suffix[] = "tcc";
static char scratch[8192];

int main(void) {
	scratch[0] = '-';
	scratch[1] = 'f';
	scratch[2] = 'r';
	scratch[3] = 'o';
	scratch[4] = 'm';
	scratch[5] = '-';
	write(1, prefix, 5);
	write(1, scratch, 6);
	write(1, suffix, 3);
	write(1, "\n", 1);
	return 0;
}
