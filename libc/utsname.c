/*
 * uname(2) — pure user-space implementation.
 *
 * Returns hardcoded identification strings without involving the kernel.
 * The "release" and "version" strings are static; bump them when the
 * kernel changes substantively. If we ever add a SYS_uname syscall the
 * kernel can supply runtime build info instead.
 */
#include <sys/utsname.h>
#include <errno.h>
#include <stddef.h>

static void copy_field(char *dst, const char *src) {
    size_t i = 0;
    while (i < UTSNAME_LEN - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

int uname(struct utsname *buf) {
    if (!buf) return -EFAULT;
    copy_field(buf->sysname,  "SBUnix");
    copy_field(buf->nodename, "sbunix");
    copy_field(buf->release,  "0.9");
    copy_field(buf->version,  "dev");
    copy_field(buf->machine,  "riscv64");
    return 0;
}
