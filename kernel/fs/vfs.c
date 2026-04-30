#include <vfs.h>
#include <inode.h>
#include <errno.h>
#include <string.h>
#include <printk.h>
#include <proc.h>

#define NMOUNT 8
#define SYMLINK_MAX 8

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
    /* Keep mp alive: child root holds a borrowed pointer back to its
     * mount point so ".." can cross the mount boundary upward. No extra
     * ref is taken (the mount is permanent for the life of the kernel). */
    root->mount_parent = mp;
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
static int namei_flags(const char *path, struct inode **out, int nofollow);

int namei(const char *path, struct inode **out) {
    return namei_flags(path, out, 0);
}

int lnamei(const char *path, struct inode **out) {
    return namei_flags(path, out, 1);
}

static int namei_flags(const char *path, struct inode **out, int nofollow) {
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

    int hops = 0;
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

        /* Crossing ".." out of a mount root: step up to the mount point
         * inode in the host fs, then let the lookup below resolve its
         * parent. Without this, sbfs_lookup(dir, "..") would just return
         * the sbfs root again and cd .. would never leave the mount. */
        if (start[0] == '.' && start[1] == '.' && (p - start) == 2 &&
            cur->mount_parent) {
            struct inode *mp = inode_get(cur->mount_parent);
            inode_put(cur);
            cur = mp;
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

        /* Symlink following. We follow when:
         *   - the resolved inode is a symlink, AND
         *   - this is not the final component, OR nofollow == 0.
         * Track hops to detect loops. */
        int is_final = (*p == '\0');
        if (cur->type == I_LNK && !(is_final && nofollow)) {
            if (!cur->ops->readlink) { inode_put(cur); return -EINVAL; }
            if (++hops > SYMLINK_MAX) { inode_put(cur); return -ELOOP; }

            char target[256];
            int tlen = cur->ops->readlink(cur, target, sizeof(target) - 1);
            if (tlen < 0) { inode_put(cur); return tlen; }
            target[tlen] = '\0';
            inode_put(cur);

            /* Build the new path: target + remaining (whatever is after p). */
            int remlen = 0; while (p[remlen]) remlen++;
            int total = tlen + (remlen ? 1 + remlen : 0);
            if (total >= (int)sizeof(buf)) return -ENAMETOOLONG;

            char merged[256];
            int mi = 0;
            for (int i = 0; i < tlen; i++) merged[mi++] = target[i];
            if (remlen) {
                merged[mi++] = '/';
                for (int i = 0; i < remlen; i++) merged[mi++] = p[i];
            }
            merged[mi] = '\0';
            for (int i = 0; i <= mi; i++) buf[i] = merged[i];

            /* Restart the walk. Absolute target → from root mount;
             * relative target → from where the symlink lived (current
             * directory of the resolution; we approximate by restarting
             * from the parent of the link, which is the caller's cwd or
             * the previous component's parent. Simplification: restart
             * from root for absolute, from cwd for relative — same
             * convention `namei` uses for absolute vs relative paths). */
            if (buf[0] == '/') {
                cur = inode_get(mounts[0].root);
                p   = buf + 1;
            } else {
                struct pcb *proc = current_proc();
                if (!proc || !proc->cwd) return -ENOENT;
                cur = inode_get(proc->cwd);
                p   = buf;
            }
            continue;
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
