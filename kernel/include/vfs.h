#pragma once
#include <inode.h>

/* Resolve an absolute or relative path to an inode.
 * On success: *out is set (refcount bumped) and 0 is returned.
 * On failure: negative -errno is returned.
 * Caller must call inode_put(*out) when done. */
int namei(const char *path, struct inode **out);

/* Like namei, but does not follow a trailing symlink component. */
int lnamei(const char *path, struct inode **out);

/* Register a mounted filesystem root at the given path.
 * The path must already exist in the VFS tree (or be "/" for the root mount).
 * Returns 0 on success, -errno on failure. */
int mount_fs(const char *path, struct inode *root);

#include <stdint.h>

/* Read up to n bytes from ip at off into buf via the page cache.
 * Returns bytes read, or negative -errno. The inode's ops->readpage
 * MUST be set. */
int generic_file_read(struct inode *ip, uint64_t off,
                      void *buf, uint64_t n);

/* Write up to n bytes via the page cache (write-through).
 * Returns bytes written, or negative -errno. Caller wraps begin_op
 * for sbfs (the helper itself does not call begin_op/end_op).
 * The inode's ops->writepage MUST be set. */
int generic_file_write(struct inode *ip, uint64_t off,
                       const void *buf, uint64_t n);

struct vma;
/* Page-fault path for VMA_TYPE_FILE. Returns 0 on success, -1 on failure
 * (caller raises SIGBUS / kills process). */
int generic_file_fault(struct vma *v, uint64_t fault_va);
