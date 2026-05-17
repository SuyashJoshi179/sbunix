#pragma once
#include <stdint.h>
#include <inode.h>

/* -----------------------------------------------------------------------
 * sbfs v2 on-disk constants  (must match tools/mkfs.c exactly)
 * ----------------------------------------------------------------------- */
#define SBFS_MAGIC       0x53425632u   /* "SBV2" — v2 added mode/uid/gid */
#define SBFS_BSIZE       512           /* bytes per block                 */
#define SBFS_NDIRECT     10            /* total addr slots per inode (disk format) */
#define SBFS_NDIR        8             /* direct block slots (addrs[0..7])  */
#define SBFS_NINDIR      2             /* indirect block slots (addrs[8..9]) */
#define SBFS_NBLK_PER_INDIR  (SBFS_BSIZE / 4)  /* 128 block addrs per indirect block */
#define SBFS_NINODES     256
#define SBFS_LOGSIZE     16
#define SBFS_DIRSIZ      14            /* max name length in a dirent     */
#define SBFS_ROOTINUM    1             /* inode number of the root dir    */

/* Max file size: 8 direct + 2*128 indirect = 264 blocks = 132 KiB */
#define SBFS_MAX_FILE_SIZE  ((SBFS_NDIR + SBFS_NINDIR * SBFS_NBLK_PER_INDIR) * SBFS_BSIZE)

/* -----------------------------------------------------------------------
 * On-disk superblock (stored in block 1)
 * ----------------------------------------------------------------------- */
struct sb_superblock {
    uint32_t magic;
    uint32_t size;        /* total blocks in image                        */
    uint32_t nblocks;     /* data blocks                                  */
    uint32_t ninodes;
    uint32_t nlog;
    uint32_t logstart;
    uint32_t inodestart;
    uint32_t bmapstart;
};

/* -----------------------------------------------------------------------
 * On-disk inode (exactly 64 bytes → 8 per 512-byte block)
 *   type(2)+nlink(2)+size(4)+mtime(8)+mode(4)+uid(2)+gid(2)+addrs[10](40) = 64
 *
 * mode/uid/gid were added in v2. v1 images (magic SBV1) had addrs[12]
 * occupying the bytes now taken by mode/uid/gid plus addrs[0..1]; the
 * magic bump in sbfs_init rejects them so the layout cannot be misread.
 * ----------------------------------------------------------------------- */
struct sb_dinode {
    uint16_t type;                    /* 0=free, 1=file, 2=dir, 3=symlink */
    uint16_t nlink;
    uint32_t size;                    /* file size in bytes            */
    uint64_t mtime;                   /* modification time (seconds)   */
    uint32_t mode;                    /* POSIX mode bits (S_IF* | perms) */
    uint16_t uid;
    uint16_t gid;
    uint32_t addrs[SBFS_NDIRECT];    /* direct + indirect block addresses */
};

/* -----------------------------------------------------------------------
 * On-disk directory entry (16 bytes → 32 per block)
 * ----------------------------------------------------------------------- */
struct sb_dirent {
    uint16_t inum;                    /* 0 = empty slot                */
    char     name[SBFS_DIRSIZ];
};

/* -----------------------------------------------------------------------
 * In-memory inode cache entry
 *
 * struct inode is embedded first so we can cast between the two.
 * ----------------------------------------------------------------------- */
#define SBFS_ICACHE_MAX  64

struct sbfs_inode {
    struct inode    vnode;   /* MUST be first — cast to/from struct inode * */
    uint32_t        inum;
    int             valid;   /* dinode has been read from disk              */
    int             dirty;   /* dinode needs writeback on release           */
    struct sb_dinode d;      /* cached on-disk inode                       */
};

/* -----------------------------------------------------------------------
 * Public interface
 * ----------------------------------------------------------------------- */

/* Initialise sbfs in-memory state: read superblock, replay log.
 * Returns 0 on success, negative errno on failure. */
int sbfs_init(void);

/* Register the sbfs root at `target` (e.g. "/mnt"). Must be called
 * after sbfs_init() has succeeded. Returns 0 / -errno. */
int sbfs_attach(const char *target);

/* Get an in-memory inode for the given inode number (bumps refcnt). */
struct inode *sbfs_iget(uint32_t inum);

/* Allocate a new inode of the given type (begin/end_op must surround this). */
struct inode *sbfs_ialloc(uint16_t type);

/* Update on-disk inode from in-memory cache (call inside a transaction). */
void sbfs_iupdate(struct sbfs_inode *si);

/* Read n bytes from inode starting at off into buf. Returns bytes read. */
int sbfs_readi(struct inode *ip, uint64_t off, void *buf, uint64_t n);

/* Write n bytes from buf into inode starting at off. Returns bytes written. */
int sbfs_writei(struct inode *ip, uint64_t off, const void *buf, uint64_t n);

/* Look up name in dir, returning inode (refcnt bumped) or NULL. */
struct inode *sbfs_dirlookup(struct inode *dir, const char *name);

/* Add a (name, inum) entry to dir (inside a transaction). */
int sbfs_dirlink(struct inode *dir, const char *name, uint32_t inum);

/* Remove entry named name from dir (inside a transaction). Returns 0 or -errno. */
int sbfs_dirunlink(struct inode *dir, const char *name);

/* Create name in parent dir, allocate inode of type; returns new inode. */
struct inode *sbfs_create(struct inode *parent, const char *name, uint16_t type);

/* Unlink name from parent dir, decrement nlink; free if 0 and no open refs. */
int sbfs_unlink(struct inode *parent, const char *name);
