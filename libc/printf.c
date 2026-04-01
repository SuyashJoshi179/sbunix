#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>

int printf(const char *fmt, ...) {
  va_list args;
  int written = 0;

  va_start(args, fmt);

  while (*fmt) {
    if (*fmt == '%' && *(fmt + 1) == 's') {
      const char *s = va_arg(args, const char *);
      if (s == 0) {
        s = "(null)";
      }
      while (*s) {
        write(1, s, 1);
        s++;
        written++;
      }
      fmt += 2;
      continue;
    }

    if (*fmt == '%' && *(fmt + 1) == '%') {
      write(1, "%", 1);
      written++;
      fmt += 2;
      continue;
    }

    write(1, fmt, 1);
    fmt++;
    written++;
  }

  va_end(args);
  return written;
}
