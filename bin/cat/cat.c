#include <stdio.h>
#include <unistd.h>

static int cat_fd(int fd) {
  char buf[128];
  while (1) {
    long n = read(fd, buf, sizeof(buf));
    if (n < 0) {
      return -1;
    }
    if (n == 0) {
      return 0;
    }
    if (write(1, buf, (unsigned long)n) != n) {
      return -1;
    }
  }
}

int main(int argc, char *argv[]) {
  if (argc <= 1) {
    printf("usage: cat <path>...\n");
    return 1;
  }

  int rc = 0;
  for (int i = 1; i < argc; i++) {
    long fd = open(argv[i]);
    if (fd < 0) {
      printf("cat: cannot open %s\n", argv[i]);
      rc = 1;
      continue;
    }

    if (cat_fd((int)fd) < 0) {
      printf("cat: read error on %s\n", argv[i]);
      rc = 1;
    }

    close((int)fd);
  }

  return rc;
}
