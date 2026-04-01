#include <stdio.h>
#include <unistd.h>

static int is_space(char c) {
  return c == ' ' || c == '\t';
}

static int str_eq(const char *a, const char *b) {
  int i = 0;
  while (a[i] && b[i]) {
    if (a[i] != b[i]) return 0;
    i++;
  }
  return a[i] == '\0' && b[i] == '\0';
}

static int parse_args(char *line, char **argv, int max_args) {
  int argc = 0;
  int i = 0;

  while (line[i] && argc < max_args) {
    while (is_space(line[i])) i++;
    if (!line[i]) break;

    argv[argc++] = &line[i];
    while (line[i] && !is_space(line[i])) i++;
    if (line[i]) {
      line[i++] = '\0';
    }
  }

  return argc;
}

static void run_rc_line(char *line) {
  char *argv[8];

  while (*line && is_space(*line)) line++;
  if (*line == '\0' || *line == '#') {
    return;
  }

  int argc = parse_args(line, argv, 8);
  if (argc == 0) {
    return;
  }

  if (str_eq(argv[0], "echo")) {
    for (int i = 1; i < argc; i++) {
      if (i > 1) printf(" ");
      printf("%s", argv[i]);
    }
    printf("\n");
    return;
  }

  if (str_eq(argv[0], "exec")) {
    if (argc < 2) {
      printf("init: exec missing path\n");
      return;
    }
    if (exec(argv[1]) < 0) {
      printf("init: exec failed %s\n", argv[1]);
    }
    return;
  }

  printf("init: unknown rc command %s\n", argv[0]);
}

int main(int argc, char *argv[]) {
  char buf[512];
  long fd = open("/etc/rc");
  if (fd < 0) {
    printf("init: open /etc/rc failed\n");
    exec("/bin/sh");
    return 1;
  }

  long n = read((int)fd, buf, sizeof(buf) - 1);
  close((int)fd);
  if (n <= 0) {
    printf("init: empty /etc/rc\n");
    exec("/bin/sh");
    return 1;
  }

  buf[n] = '\0';
  char *line = buf;
  for (long i = 0; i <= n; i++) {
    if (buf[i] == '\n' || buf[i] == '\0') {
      buf[i] = '\0';
      run_rc_line(line);
      line = &buf[i + 1];
    }
  }

  exec("/bin/sh");
  printf("init: failed to start /bin/sh\n");
  return 1;
}
