#pragma once
#include <stdint.h>

struct inode;
struct stat;

struct inode_ops {
    int  (*read)    (struct inode *, uint64_t off, void *buf, uint64_t n);
    int  (*write)   (struct inode *, uint64_t off, const void *buf, uint64_t n);
    int  (*ioctl)   (struct inode *, int cmd, unsigned long arg);
    int  (*stat)    (struct inode *, struct stat *);
    int  (*lookup)  (struct inode *dir, const char *name, struct inode **out);
    int  (*getdents)(struct inode *dir, uint64_t off, void *buf, uint64_t n,
                     uint64_t *out_next);
    /* Free all data blocks and reset size to 0 (for O_TRUNC).
     * Caller must wrap in begin_op/end_op.  NULL for read-only filesystems. */
    int  (*truncate)(struct inode *);
    /* Called when refcnt drops to 0; may write back dirty state.
     * NULL for static-pool filesystems (tarfs, devfs). */
    void (*release) (struct inode *);
    /* Read the target of a symbolic link into `buf`. Returns the number
     * of bytes written (no NUL terminator), or -errno. NULL for non-link
     * filesystems; namei treats that as -EINVAL. */
    int  (*readlink)(struct inode *ip, char *buf, uint64_t n);
    /* Mutating directory ops.  NULL on read-only filesystems → caller
     * should treat NULL as -EROFS.  Implementations are responsible for
     * their own begin_op/end_op (callers must NOT wrap). */
    int  (*create)  (struct inode *parent, const char *name, struct inode **out);
    int  (*mkdir)   (struct inode *parent, const char *name);
    int  (*unlink)  (struct inode *parent, const char *name);
    /* Hard link: add a new directory entry `name` in `parent` referencing
     * the existing `target` inode. Caller (sys_link) guarantees same fs,
     * non-directory target, and that `name` does not already exist. */
    int  (*link)    (struct inode *parent, struct inode *target, const char *name);
    /* Atomic rename: move (old_parent, old_name) to (new_parent, new_name).
     * Caller (sys_rename) guarantees same fs and that both parents are dirs.
     * Implementation handles: replace if newpath exists, ".." fixup for
     * directory cross-parent moves, and loop prevention. */
    int  (*rename)  (struct inode *old_parent, const char *old_name,
                     struct inode *new_parent, const char *new_name);
};

#define I_REG  1
#define I_DIR  2
#define I_CHR  3
#define I_LNK  4

struct inode {
    int               type;
    uint32_t          mode;
    uint32_t          uid, gid;
    uint64_t          size;
    uint64_t          mtime;
    uint32_t          nlink;
    int               refcnt;
    const struct inode_ops *ops;
    void             *fs_data;
    struct inode     *mount_child;
    /* Non-null on a mount-root inode: points at the inode in the host fs
     * that was covered by this mount. Used so that namei can resolve ".."
     * out of a mount root back into the host filesystem. */
    struct inode     *mount_parent;
};

/* dirent64 record as written by getdents64 -- matches Linux struct dirent64. */
struct dirent64 {
    uint64_t d_ino;
    uint64_t d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    char     d_name[];   /* null-terminated basename */
};
/* d_type values */
#define DT_UNKNOWN  0
#define DT_CHR      2
#define DT_DIR      4
#define DT_REG      8

/* offset of d_name in struct dirent64 (8+8+2+1 = 19) */
#define DIRENT64_FIXED_LEN  19

struct inode *inode_get(struct inode *ip);
void          inode_put(struct inode *ip);
