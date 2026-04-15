#include <vfs.h>
#include <inode.h>
#include <errno.h>
#include <string.h>
#include <printk.h>
#include <proc.h>

#define NMOUNT 8

static struct {
    const char   *path;
    struct inode *root;
} mounts[NMOUNT];

static int nmounts = 0;

/* ----------------------------------------------------------------
 * mount_fs — register a filesystem at a path
 *
 * For the root mount ("/"), simply record mounts[0].
 * For other mounts, walk to the existing path inode and set its
 * mount_child so the path walker crosses the boundary automatically.
 * ---------------------------------------------------------------- */
int mount_fs(const char *path, struct inode *root) {
    if (nmounts >= NMOUNT) return -ENOMEM;
    mounts[nmounts].path = path;
    mounts[nmounts].root = root;
    nmounts++;

    if (path[0] == '/' && path[1] == '\0') {
        // Root mount — no mount_child needed.
        return 0;
    }

    // Non-root: find the mount point inode and hook mount_child.
    struct inode *mp;
    int rc = namei(path, &mp);
    if (rc < 0) {
        printk("mount_fs: can't resolve '%s': %d\n", path, rc);
        nmounts--;
        return rc;
    }
    mp->mount_child = root;
    inode_put(mp);
    return 0;
}

/* ----------------------------------------------------------------
 * namei — resolve an absolute or relative path to an inode
 *
 * On success: *out is set (refcount bumped), returns 0.
 * On failure: returns negative errno, *out is unchanged.
 * Caller must call inode_put(*out) when done.
 * ---------------------------------------------------------------- */
int namei(const char *path, struct inode **out) {
    if (!path || !path[0]) return -ENOENT;

    int pathlen = 0;
    while (path[pathlen]) pathlen++;
    if (pathlen >= 256) return -ENAMETOOLONG;

    char buf[256];
    for (int i = 0; i <= pathlen; i++) buf[i] = path[i];

    struct inode *cur;
    char *p;

    if (buf[0] == '/') {
        if (nmounts == 0) return -ENOENT;
        cur = mounts[0].root;
        p   = buf + 1;
    } else {
        struct pcb *proc = current_proc();
        if (!proc || !proc->cwd) return -ENOENT;
        cur = proc->cwd;
        p   = buf;
    }

    cur = inode_get(cur);

    while (*p) {
        // Traverse any mount overlay on cur.
        while (cur->mount_child) {
            struct inode *mc = inode_get(cur->mount_child);
            inode_put(cur);
            cur = mc;
        }

        // Skip consecutive slashes.
        while (*p == '/') p++;
        if (!*p) break;

        // Extract next path component.
        char *start = p;
        while (*p && *p != '/') p++;
        char saved = *p;
        *p = '\0';

        if (cur->type != I_DIR) {
            inode_put(cur);
            return -ENOTDIR;
        }

        struct inode *next = 0;
        int rc = cur->ops->lookup(cur, start, &next);
        *p = saved;

        if (rc < 0) {
            inode_put(cur);
            return rc;
        }

        inode_put(cur);
        cur = next;  // lookup bumped refcnt on next

        // Cross any mount on the newly resolved inode.
        while (cur->mount_child) {
            struct inode *mc = inode_get(cur->mount_child);
            inode_put(cur);
            cur = mc;
        }
    }

    // Final mount overlay.
    while (cur->mount_child) {
        struct inode *mc = inode_get(cur->mount_child);
        inode_put(cur);
        cur = mc;
    }

    *out = cur;
    return 0;
}
