/*
 * log.c — Write-ahead log for sbfs v1
 *
 * On-disk log layout (beginning at logstart):
 *   block logstart+0    : log header
 *   block logstart+1..N : log data blocks (up to log_size-1 of them)
 *
 * Log header (fits in one block):
 *   uint32_t n          : number of committed log blocks (0 = no pending txn)
 *   uint32_t block[N]   : real disk block numbers for each log data block
 *
 * A transaction is committed by writing the log header with n>0.
 * Installation copies log data to real locations then zeros n in the header.
 * If we crash after writing the header but before zeroing it, recover_from_log
 * replays the transaction on next mount.
 */

#include <log.h>
#include <bio.h>
#include <printk.h>
#include <string.h>
#include <proc.h>

/* -----------------------------------------------------------------------
 * On-disk log header layout
 * ----------------------------------------------------------------------- */
#define LOG_HDR_MAX  15   /* must be < log_size (we use log_size-1 data slots) */

struct log_header {
    uint32_t n;               /* number of blocks in this transaction */
    uint32_t block[LOG_HDR_MAX]; /* real block numbers                */
};

/* -----------------------------------------------------------------------
 * In-memory log state
 * ----------------------------------------------------------------------- */
static struct {
    uint32_t start;       /* first block of the log on disk          */
    uint32_t size;        /* total log blocks (including header)     */
    int      outstanding; /* 1 = inside begin_op/end_op              */
    int      nblocks;     /* blocks in current (pending) transaction */
    uint32_t blocks[LOG_HDR_MAX]; /* real block numbers              */
    /* Cached dirty data (parallel to blocks[]) — we hold bread refs */
    struct buf *bufs[LOG_HDR_MAX];
} log;

/* -----------------------------------------------------------------------
 * Helpers
 * ----------------------------------------------------------------------- */

static void write_log_header(void) {
    struct buf *hb = bread(log.start);
    struct log_header *lh = (struct log_header *)hb->data;
    lh->n = log.nblocks;
    for (int i = 0; i < log.nblocks; i++)
        lh->block[i] = log.blocks[i];
    bwrite(hb);
    brelse(hb);
}

static void install_trans(void) {
    struct buf *lb = 0;
    for (int i = 0; i < log.nblocks; i++) {
        lb = bread(log.start + 1 + i);     /* log data block */
        struct buf *db = bread(log.blocks[i]);  /* real block */
        memcpy(db->data, lb->data, BSIZE);
        bwrite(db);
        brelse(lb);
        brelse(db);
    }
}

/* -----------------------------------------------------------------------
 * log_init — called from sbfs_mount
 * ----------------------------------------------------------------------- */
void log_init(uint32_t log_start, uint32_t log_size) {
    log.start       = log_start;
    log.size        = log_size;
    log.outstanding = 0;
    log.nblocks     = 0;
}

/* -----------------------------------------------------------------------
 * recover_from_log — replay any uncommitted (crashed) transaction
 * ----------------------------------------------------------------------- */
void recover_from_log(void) {
    struct buf *hb = bread(log.start);
    struct log_header *lh = (struct log_header *)hb->data;

    log.nblocks = lh->n;
    for (int i = 0; i < (int)lh->n; i++)
        log.blocks[i] = lh->block[i];
    brelse(hb);

    if (log.nblocks > 0) {
        printk("log: replaying %d blocks from crash\n", log.nblocks);
        install_trans();
        log.nblocks = 0;
        write_log_header();  /* clear n → 0 */
    }
}

/* -----------------------------------------------------------------------
 * begin_op — start a transaction
 * ----------------------------------------------------------------------- */
void begin_op(void) {
    /* Serialize transactions: if another proc holds the log, sleep until
     * it calls end_op. Single-hart kernel yields inside bread (disk I/O)
     * so two procs can race here; use the log.outstanding flag as a
     * sleep channel. */
    while (log.outstanding)
        proc_sleep_chan(&log.outstanding);
    log.outstanding = 1;
    log.nblocks     = 0;
    for (int i = 0; i < LOG_HDR_MAX; i++)
        log.bufs[i] = 0;
}

/* -----------------------------------------------------------------------
 * log_write — add a dirty buffer to the current transaction.
 * The buffer MUST already be obtained via bread(); we hold the ref.
 * Duplicate log_write for the same blockno is a no-op.
 * ----------------------------------------------------------------------- */
void log_write(struct buf *b) {
    if (!log.outstanding)
        panic("log_write outside transaction");
    if (log.nblocks >= LOG_HDR_MAX)
        panic("log overflow: too many blocks in one transaction");

    /* Deduplicate: if blockno already in this transaction, update in place. */
    for (int i = 0; i < log.nblocks; i++) {
        if (log.blocks[i] == b->blockno) {
            log.bufs[i] = b;   /* update pointer to newest dirty copy */
            return;
        }
    }

    int slot = log.nblocks++;
    log.blocks[slot] = b->blockno;
    log.bufs[slot]   = b;
    b->dirty = 1;   /* prevent brelse/eviction from flushing early */
}

/* -----------------------------------------------------------------------
 * end_op — commit the transaction
 *
 *   1. Write each dirty buffer into a log data block.
 *   2. Flush log header with n = nblocks  → crash-safe commit point.
 *   3. Install each log block to its real location.
 *   4. Zero the log header (n = 0).
 *   5. Clear dirty flags and release bread refs.
 * ----------------------------------------------------------------------- */
void end_op(void) {
    if (!log.outstanding)
        panic("log: end_op without begin_op");

    /* Step 1: copy dirty data to log data blocks. */
    for (int i = 0; i < log.nblocks; i++) {
        struct buf *lb = bread(log.start + 1 + i);
        memcpy(lb->data, log.bufs[i]->data, BSIZE);
        bwrite(lb);
        brelse(lb);
    }

    /* Step 2: write the log header — this is the commit point. */
    write_log_header();

    /* Step 3: install (copy log blocks → real locations). */
    install_trans();

    /* Step 4: zero the log header. */
    log.nblocks = 0;
    write_log_header();

    /* Step 5: clear dirty flags.
     * The filesystem code called brelse() after log_write(), so refcnt is
     * already 0 here.  install_trans() also called bwrite() which clears
     * dirty.  Just null out the pointers — do NOT call brelse() again. */
    for (int i = 0; i < LOG_HDR_MAX; i++) {
        if (log.bufs[i]) {
            log.bufs[i]->dirty = 0;
            log.bufs[i] = 0;
        }
    }

    log.outstanding = 0;
    proc_wakeup_chan(&log.outstanding);
}
