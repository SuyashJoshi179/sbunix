#pragma once
#include <sys/types.h>
#include <stddef.h>

struct passwd {
    char  *pw_name;
    char  *pw_passwd;
    uid_t  pw_uid;
    gid_t  pw_gid;
    char  *pw_gecos;
    char  *pw_dir;
    char  *pw_shell;
};

struct passwd *getpwuid(uid_t uid);
struct passwd *getpwnam(const char *name);
int getpwuid_r(uid_t uid, struct passwd *pw, char *buf, size_t buflen, struct passwd **result);
int getpwnam_r(const char *name, struct passwd *pw, char *buf, size_t buflen, struct passwd **result);
void setpwent(void);
void endpwent(void);
struct passwd *getpwent(void);
