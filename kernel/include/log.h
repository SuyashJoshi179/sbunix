#pragma once
#include <bio.h>
#include <stdint.h>

/* Write-ahead log for sbfs v1.
 *
 * A single global transaction is allowed at a time.  Sequence:
 *   begin_op()
 *   b = bread(n);  modify b->data;  log_write(b);  brelse(b);
 *   ...
 *   end_op()   -- commits: writes log header, replays blocks to their
 *                           real disk locations, zeroes log header.
 *
 * On boot, recover_from_log() replays any transaction that was committed
 * (header present) but not yet installed.
 */

/* `disk_size` is the total number of blocks on the backing device.
 * recover_from_log uses it to bound-check on-disk log entries; without
 * it, a corrupt header could ask install_trans to overwrite arbitrary
 * memory-mapped or out-of-range sectors. */
void log_init(uint32_t log_start, uint32_t log_size, uint32_t disk_size);
void recover_from_log(void);

void begin_op(void);
void log_write(struct buf *b);   /* add buffer to current transaction  */
void end_op(void);               /* commit + install + clear           */
