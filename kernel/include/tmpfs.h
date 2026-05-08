#pragma once
#include <stdint.h>

/*
 * tmpfs — in-memory filesystem for /tmp.
 *
 * Files live entirely in kernel RAM (page_alloc-backed). Nothing is
 * persisted to disk; the FS is empty after every reboot. Same VFS
 * inode_ops dispatch as sbfs/devfs/tarfs, so all generic file ops
 * (open/read/write/mkdir/unlink/link/rename/stat) work transparently.
 */

#define TMPFS_NINODES        64           /* hard cap on file+dir count   */
#define TMPFS_DIRSIZ         28           /* max name length in a dirent  */
#define TMPFS_MAX_FILESIZE   (256 * 1024) /* per-file 256 KiB cap         */

/* One-shot init: zero the inode pool, prepare the root directory.
 * Called from kernel.c during boot. */
void tmpfs_init(void);

/* Mount the tmpfs root at `target`. Called from sys_mount when fstype
 * is "tmpfs". Returns 0 on success or a negative errno. */
int  tmpfs_attach(const char *target);
