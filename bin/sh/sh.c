#include <stdio.h>
#include <unistd.h>

static const char *shell_bin_names[] = {"echo", "sh", "init", "cat", "ls"};
static const int shell_bin_count = (int)(sizeof(shell_bin_names) / sizeof(shell_bin_names[0]));

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

static void shell_ls(const char *target) {
  if (target == 0) {
    target = "/bin";
  }

  if (str_eq(target, "/bin")) {
    for (int i = 0; i < shell_bin_count; i++) {
      printf("%s\n", shell_bin_names[i]);
    }
    return;
  }

  printf("ls: only /bin is supported\n");
}

static int shell_cat(const char *path) {
  char buf[128];
  long fd = open(path);
  if (fd < 0) {
    printf("cat: cannot open %s\n", path);
    return -1;
  }

  while (1) {
    long n = read((int)fd, buf, sizeof(buf));
    if (n < 0) {
      printf("cat: read error on %s\n", path);
      close((int)fd);
      return -1;
    }
    if (n == 0) {
      break;
    }
    if (write(1, buf, (unsigned long)n) != n) {
      printf("cat: write error\n");
      close((int)fd);
      return -1;
    }
  }

  close((int)fd);
  return 0;
}

static int parse_args(char *line, char **argv, int max_args) {
  int argc = 0;
  int i = 0;

  while (line[i] && argc < max_args) {
    while (is_space(line[i])) i++;
    if (!line[i]) break;

    if (line[i] == '\'' || line[i] == '"') {
      char quote = line[i++];
      argv[argc++] = &line[i];
      while (line[i] && line[i] != quote) i++;
      if (line[i] == quote) {
        line[i++] = '\0';
      }
    } else {
      argv[argc++] = &line[i];
      while (line[i] && !is_space(line[i])) i++;
      if (line[i]) {
        line[i++] = '\0';
      }
    }
  }

  return argc;
}

int main(int argc, char *argv[]) {
  char line[128];
  char *args[16];
  char exec_path[128];
  printf("sbunix shell ready\n");

  while (1) {
    printf("sbunix$ ");

    int pos = 0;
    while (pos < (int)sizeof(line) - 1) {
      char c = 0;
      long n = read(0, &c, 1);
      if (n <= 0) {
        continue;
      }

      if (c == '\b' || c == 127) {
        if (pos > 0) {
          pos--;
          write(1, "\b \b", 3);
        }
        continue;
      }

      if (c == '\r' || c == '\n') {
        break;
      }

      line[pos++] = c;
      write(1, &c, 1);
    }
    line[pos] = '\0';
    printf("\n");

    if (pos == 0) {
      continue;
    }

    int argn = parse_args(line, args, 16);
    if (argn == 0) {
      continue;
    }

    if (str_eq(args[0], "echo")) {
      for (int i = 1; i < argn; i++) {
        if (i > 1) {
          printf(" ");
        }
        printf("%s", args[i]);
      }
      printf("\n");
      continue;
    }

    if (str_eq(args[0], "help")) {
      printf("commands: help, echo <text>, cat <path>, ls, exit\n");
      continue;
    }

    if (str_eq(args[0], "exit")) {
      return 0;
    }

    if (str_eq(args[0], "cat")) {
      if (argn < 2) {
        printf("usage: cat <path>\n");
        continue;
      }
      for (int i = 1; i < argn; i++) {
        shell_cat(args[i]);
      }
      continue;
    }

    if (str_eq(args[0], "ls")) {
      if (argn > 1) {
        shell_ls(args[1]);
      } else {
        shell_ls("/bin");
      }
      continue;
    }

    int p = 0;
    if (args[0][0] == '/') {
      while (args[0][p] && p < (int)sizeof(exec_path) - 1) {
        exec_path[p] = args[0][p];
        p++;
      }
      exec_path[p] = '\0';
    } else {
      const char *prefix = "/bin/";
      for (int i = 0; prefix[i] && p < (int)sizeof(exec_path) - 1; i++) {
        exec_path[p++] = prefix[i];
      }
      for (int i = 0; args[0][i] && p < (int)sizeof(exec_path) - 1; i++) {
        exec_path[p++] = args[0][i];
      }
      exec_path[p] = '\0';
    }

    if (exec_path[0] != '\0' && exec(exec_path) == 0) {
      continue;
    }

    printf("unknown command: %s\n", args[0]);
  }

  return 0;
}
