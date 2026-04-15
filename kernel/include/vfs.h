#pragma once
#include <inode.h>

/* Resolve an absolute or relative path to an inode.
 * On success: *out is set (refcount bumped) and 0 is returned.
 * On failure: negative -errno is returned.
 * Caller must call inode_put(*out) when done. */
int namei(const char *path, struct inode **out);

/* Register a mounted filesystem root at the given path.
 * The path must already exist in the VFS tree (or be "/" for the root mount).
 * Returns 0 on success, -errno on failure. */
int mount_fs(const char *path, struct inode *root);
