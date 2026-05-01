#include <pwd.h>
#include <grp.h>
#include <string.h>
#include <errno.h>

/* Single static "root" entry. SBUnix has no /etc/passwd or /etc/group;
 * tools that simply want a name for uid 0 (ls -l, ps) get a sensible
 * answer. Calls for other uids/names return NULL. */

static char     pw_name[]  = "root";
static char     pw_pass[]  = "x";
static char     pw_gecos[] = "root";
static char     pw_dir[]   = "/";
static char     pw_shell[] = "/bin/sh";

static struct passwd _root_pw = {
    pw_name, pw_pass, 0, 0, pw_gecos, pw_dir, pw_shell,
};

static char    *gr_mem_root[] = { pw_name, 0 };
static char     gr_name[]     = "root";
static char     gr_pass[]     = "x";
static struct group _root_gr  = { gr_name, gr_pass, 0, gr_mem_root };

/* End-of-stream cursor for getpwent/getgrent. Reset by setpwent/setgrent. */
static int pw_cursor = 0;
static int gr_cursor = 0;

struct passwd *getpwuid(uid_t uid) {
    if (uid == 0) return &_root_pw;
    return 0;
}

struct passwd *getpwnam(const char *name) {
    if (name && strcmp(name, "root") == 0) return &_root_pw;
    return 0;
}

int getpwuid_r(uid_t uid, struct passwd *pw, char *buf, size_t buflen,
               struct passwd **result) {
    (void)buf; (void)buflen;
    if (!pw || !result) return EINVAL;
    if (uid != 0) { *result = 0; return 0; }
    *pw = _root_pw;
    *result = pw;
    return 0;
}

int getpwnam_r(const char *name, struct passwd *pw, char *buf, size_t buflen,
               struct passwd **result) {
    (void)buf; (void)buflen;
    if (!pw || !result) return EINVAL;
    if (!name || strcmp(name, "root") != 0) { *result = 0; return 0; }
    *pw = _root_pw;
    *result = pw;
    return 0;
}

void setpwent(void) { pw_cursor = 0; }
void endpwent(void) { pw_cursor = 1; }
struct passwd *getpwent(void) {
    if (pw_cursor++) return 0;
    return &_root_pw;
}

struct group *getgrgid(gid_t gid) {
    return gid == 0 ? &_root_gr : 0;
}

struct group *getgrnam(const char *name) {
    if (name && strcmp(name, "root") == 0) return &_root_gr;
    return 0;
}

int getgrgid_r(gid_t gid, struct group *gr, char *buf, size_t buflen,
               struct group **result) {
    (void)buf; (void)buflen;
    if (!gr || !result) return EINVAL;
    if (gid != 0) { *result = 0; return 0; }
    *gr = _root_gr;
    *result = gr;
    return 0;
}

int getgrnam_r(const char *name, struct group *gr, char *buf, size_t buflen,
               struct group **result) {
    (void)buf; (void)buflen;
    if (!gr || !result) return EINVAL;
    if (!name || strcmp(name, "root") != 0) { *result = 0; return 0; }
    *gr = _root_gr;
    *result = gr;
    return 0;
}

void setgrent(void) { gr_cursor = 0; }
void endgrent(void) { gr_cursor = 1; }
struct group *getgrent(void) {
    if (gr_cursor++) return 0;
    return &_root_gr;
}

int getgrouplist(const char *user, gid_t group, gid_t *groups, int *ngroups) {
    (void)user;
    if (!ngroups) return -1;
    int cap = *ngroups;
    *ngroups = 1;
    if (cap < 1 || !groups) return -1;
    groups[0] = group;
    return 1;
}

int getgroups(int size, gid_t *list) {
    if (size == 0) return 1;
    if (size < 1 || !list) { errno = EINVAL; return -1; }
    list[0] = 0;
    return 1;
}
