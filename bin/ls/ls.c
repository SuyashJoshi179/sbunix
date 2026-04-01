#include <stdio.h>
#include <unistd.h>

static const char *bin_names[] = {"echo", "sh", "init", "cat", "ls", "pwd", "clear"};
static const char *etc_names[] = {"rc"};
static const int bin_name_count = (int)(sizeof(bin_names) / sizeof(bin_names[0]));
static const int etc_name_count = (int)(sizeof(etc_names) / sizeof(etc_names[0]));

static int str_eq(const char *a, const char *b) {
  int i = 0;
  while (a[i] && b[i]) {
    if (a[i] != b[i]) return 0;
    i++;
  }
  return a[i] == '\0' && b[i] == '\0';
}

static void list_known(const char *dir, const char **names, int count) {
  char path[128];

  for (int i = 0; i < count; i++) {
    int p = 0;

    for (int j = 0; dir[j] && p < (int)sizeof(path) - 1; j++) {
      path[p++] = dir[j];
    }
    if (p > 0 && path[p - 1] != '/' && p < (int)sizeof(path) - 1) {
      path[p++] = '/';
    }
    for (int j = 0; names[i][j] && p < (int)sizeof(path) - 1; j++) {
      path[p++] = names[i][j];
    }
    path[p] = '\0';

    long fd = open(path);
    if (fd >= 0) {
      printf("%s\n", names[i]);
      close((int)fd);
    }
  }
}

int main(int argc, char *argv[]) {
  const char *target = (argc > 1) ? argv[1] : "/bin";

  if (str_eq(target, "/bin")) {
    list_known("/bin", bin_names, bin_name_count);
    return 0;
  }

  if (str_eq(target, "/etc")) {
    list_known("/etc", etc_names, etc_name_count);
    return 0;
  }

  long fd = open(target);
  if (fd < 0) {
    printf("ls: cannot access %s\n", target);
    return 1;
  }
  close((int)fd);
  printf("%s\n", target);
  return 0;
}
