#include <libgen.h>
#include <string.h>

/* POSIX basename/dirname. Both may modify the input buffer. NULL or empty
 * input returns a pointer to the static string ".". */
static char dot[]   = ".";
static char slash[] = "/";

char *basename(char *path) {
    if (!path || !*path) return dot;
    /* strip trailing slashes (but keep "/" itself) */
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/') path[--len] = '\0';
    if (len == 1 && path[0] == '/') return slash;
    char *slash_at = strrchr(path, '/');
    return slash_at ? slash_at + 1 : path;
}

char *dirname(char *path) {
    if (!path || !*path) return dot;
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/') path[--len] = '\0';
    char *slash_at = strrchr(path, '/');
    if (!slash_at) return dot;
    if (slash_at == path) { path[1] = '\0'; return path; }
    /* trim trailing slashes from the directory portion */
    while (slash_at > path && *(slash_at - 1) == '/') slash_at--;
    *slash_at = '\0';
    return path;
}
