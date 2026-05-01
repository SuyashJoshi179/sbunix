#pragma once

/* Field length matches Linux convention (including trailing NUL). */
#define UTSNAME_LEN 65

struct utsname {
    char sysname[UTSNAME_LEN];   /* "SBUnix" */
    char nodename[UTSNAME_LEN];  /* host name */
    char release[UTSNAME_LEN];   /* kernel release */
    char version[UTSNAME_LEN];   /* build identifier */
    char machine[UTSNAME_LEN];   /* "riscv64" */
    char domainname[UTSNAME_LEN];
};

int uname(struct utsname *buf);
