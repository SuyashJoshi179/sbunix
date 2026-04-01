#include <unistd.h>

int main(void) {
  static const char seq[] = "\033[2J\033[H";
  long n = write(1, seq, sizeof(seq) - 1);
  return n == (long)(sizeof(seq) - 1) ? 0 : 1;
}
