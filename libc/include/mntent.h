#pragma once
#include <stdio.h>

struct mntent {
    char *mnt_fsname;
    char *mnt_dir;
    char *mnt_type;
    char *mnt_opts;
    int   mnt_freq;
    int   mnt_passno;
};

#define MOUNTED      "/etc/mtab"
#define _PATH_MOUNTED MOUNTED

#define MNTTYPE_IGNORE  "ignore"
#define MNTTYPE_NFS     "nfs"
#define MNTTYPE_SWAP    "swap"

#define MNTOPT_DEFAULTS "defaults"
#define MNTOPT_RO       "ro"
#define MNTOPT_RW       "rw"
#define MNTOPT_SUID     "suid"
#define MNTOPT_NOSUID   "nosuid"
#define MNTOPT_NOAUTO   "noauto"

FILE          *setmntent(const char *file, const char *mode);
int            endmntent(FILE *fp);
struct mntent *getmntent(FILE *fp);
struct mntent *getmntent_r(FILE *fp, struct mntent *m, char *buf, int buflen);
int            addmntent(FILE *fp, const struct mntent *m);
char          *hasmntopt(const struct mntent *m, const char *opt);
