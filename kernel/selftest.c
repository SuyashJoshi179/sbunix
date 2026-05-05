#include <bio.h>
#include <exec.h>
#include <file.h>
#include <inode.h>
#include <page_cache.h>
#include <page_ref.h>
#include <pipe.h>
#include <log.h>
#include <pmem.h>
#include <printk.h>
#include <proc.h>
#include <sbfs.h>
#include <selftest.h>
#include <signal.h>
#include <stat.h>
#include <string.h>
#include <tarfs.h>
#include <termios.h>
#include <timer.h>
#include <vfs.h>
#include <vma.h>
#include <vmem.h>

// ----------------------------------------------------------------------------
// Minimal test harness
// ----------------------------------------------------------------------------

static int st_fails = 0;
static int st_pass  = 0;

static void st_check(int cond, const char *name) {
    if (cond) {
        printk("[SELFTEST] PASS  %s\n", name);
        st_pass++;
    } else {
        printk("[SELFTEST] FAIL  %s\n", name);
        st_fails++;
    }
}

// ----------------------------------------------------------------------------
// tarfs tests
// ----------------------------------------------------------------------------

static void test_tarfs(void) {
    printk("[SELFTEST] -- tarfs --\n");

    unsigned long sz = 0;

    // Known file: bin/init must exist in the embedded tarfs.
    const void *p = tarfs_find("bin/init", &sz);
    st_check(p  != 0, "tarfs_find bin/init returns non-null");
    st_check(sz  > 0, "tarfs_find bin/init size > 0");

    // Known file: bin/fork_test must also exist.
    sz = 0;
    const void *q = tarfs_find("bin/fork_test", &sz);
    st_check(q  != 0, "tarfs_find bin/fork_test returns non-null");
    st_check(sz  > 0, "tarfs_find bin/fork_test size > 0");

    // Non-existent path must return null.
    sz = 0;
    const void *bad = tarfs_find("does/not/exist", &sz);
    st_check(bad == 0, "tarfs_find missing file returns null");
}

// ----------------------------------------------------------------------------
// alloc_proc / free_proc tests
// ----------------------------------------------------------------------------

static void test_alloc_free_proc(void) {
    printk("[SELFTEST] -- alloc_proc / free_proc --\n");

    struct pcb *a = alloc_proc();
    st_check(a != 0,              "alloc_proc returns non-null");
    st_check(a->pid > 0,          "first alloc pid > 0");
    st_check(a->kstack_page != 0, "kstack_page allocated");
    st_check(a->state == PROC_UNUSED, "initial state is PROC_UNUSED");
    st_check(a->is_user == 0,     "initial is_user == 0");

    struct pcb *b = alloc_proc();
    st_check(b != 0,            "second alloc_proc returns non-null");
    st_check(b->pid > a->pid,   "second pid > first pid");

    // Verify both appear in the list (reachable from process list root via
    // current_proc is not exposed, but we can check they are distinct objects
    // occupying distinct pages, i.e. addresses differ by at least PAGE_SIZE).
    unsigned long diff = (unsigned long)b - (unsigned long)a;
    if ((long)diff < 0) diff = (unsigned long)a - (unsigned long)b;
    st_check(diff >= PAGE_SIZE, "two PCBs occupy distinct pages");

    // free_proc removes both cleanly (no crash = pass).
    free_proc(a);
    free_proc(b);
    st_check(1, "free_proc both procs without crash");
}

// ----------------------------------------------------------------------------
// ELF loader test — load bin/init into a fresh user page table
// ----------------------------------------------------------------------------

static void test_load_elf(void) {
    printk("[SELFTEST] -- load_user_elf --\n");

    unsigned long sz = 0;
    const void *img = tarfs_find("bin/init", &sz);
    if (!img) {
        st_check(0, "load_elf: tarfs_find bin/init");
        return;
    }

    pgtable_t pt = create_user_pgtable();
    st_check(pt != 0, "load_elf: create_user_pgtable");
    if (!pt) return;

    unsigned long entry = 0;
    struct vma *vlist = 0;
    uint64_t brk = 0;
    int rc = load_user_elf(pt, img, sz, &entry, &vlist, &brk);
    st_check(rc == 0,                    "load_elf: load_user_elf returns 0");
    st_check(entry >= USER_TEXT_BASE,    "load_elf: entry >= USER_TEXT_BASE");
    st_check(entry <  KVMEM_OFFSET,      "load_elf: entry in user virtual space");
    st_check(vlist != 0,                 "load_elf: VMA list non-null");
    st_check(brk > 0,                   "load_elf: brk > 0");

    vma_list_free(&vlist);
    free_user_pgtable(pt);
    st_check(1, "load_elf: free_user_pgtable without crash");
}

// ----------------------------------------------------------------------------
// uvmcow_share test — COW share a page table and verify pages are shared
// ----------------------------------------------------------------------------

static void test_uvmcow_share(void) {
    printk("[SELFTEST] -- uvmcow_share --\n");

    unsigned long sz = 0;
    const void *img = tarfs_find("bin/init", &sz);
    if (!img) { st_check(0, "cow_share: tarfs setup"); return; }

    pgtable_t parent_pt = create_user_pgtable();
    if (!parent_pt) { st_check(0, "cow_share: parent create_user_pgtable"); return; }

    unsigned long entry = 0;
    struct vma *vlist = 0;
    uint64_t brk = 0;
    if (load_user_elf(parent_pt, img, sz, &entry, &vlist, &brk) < 0) {
        free_user_pgtable(parent_pt);
        st_check(0, "cow_share: parent load_user_elf");
        return;
    }
    vma_list_free(&vlist);
    if (!map_stack(parent_pt)) {
        free_user_pgtable(parent_pt);
        st_check(0, "cow_share: parent map_stack");
        return;
    }

    pgtable_t child_pt = uvmcow_share(parent_pt);
    st_check(child_pt != 0,              "cow_share: returns non-null");
    st_check(child_pt != parent_pt,      "cow_share: distinct page table root");

    if (child_pt) {
        pte_t *ppte = get_pte(parent_pt, entry, 0);
        pte_t *cpte = get_pte(child_pt,  entry, 0);
        st_check(ppte != 0, "cow_share: parent PTE at entry present");
        st_check(cpte != 0, "cow_share: child PTE at entry present");

        if (ppte && cpte) {
            unsigned long ppa = pte_to_phyaddr(*ppte);
            unsigned long cpa = pte_to_phyaddr(*cpte);
            st_check(ppa == cpa, "cow_share: child shares same physical page (COW)");
        }

        free_user_pgtable(child_pt);
    }
    free_user_pgtable(parent_pt);
    st_check(1, "cow_share: cleanup without crash");
}

// ----------------------------------------------------------------------------
// Leak test: spawn + tear down 1000 processes and check pmem invariant
// ----------------------------------------------------------------------------

static void test_leak_spawn_free(void) {
    printk("[SELFTEST] -- leak: spawn/free 1000x --\n");

    unsigned long before = pmem_free_count();
    int ok = 1;

    for (int i = 0; i < 1000; i++) {
        struct pcb *p = proc_spawn("bin/init");
        if (!p) {
            ok = 0;
            printk("[SELFTEST] leak: spawn failed at iter=%d\n", i);
            break;
        }
        // Tear down the address space and proc manually (mirrors what
        // free_proc now does, exercising the full allocation/free path
        // without needing the scheduler running).
        free_proc(p);
        /* Invariant: with no scheduler activity, process list must be empty. */
        if (proc_list_head() != 0) {
            printk("[SELFTEST] proc list not empty after free (iter=%d head=%p)\n",
                   i, proc_list_head());
            panic("selftest: proc list corruption");
        }
    }

    st_check(ok, "leak: spawn/free loop completed 1000x");

    unsigned long after = pmem_free_count();
    st_check(before == after, "pmem free count invariant after 1000x spawn/free");
    if (before != after)
        printk("[SELFTEST]   before=%lu after=%lu delta=%ld\n",
               before, after, (long)after - (long)before);
}

// ----------------------------------------------------------------------------
// namei tests
// ----------------------------------------------------------------------------

static void test_namei(void) {
    printk("[SELFTEST] -- namei --\n");

    struct inode *ip = 0;

    // "/" must resolve to a directory.
    int rc = namei("/", &ip);
    st_check(rc == 0, "namei '/' returns 0");
    st_check(ip != 0, "namei '/' non-null");
    if (ip) { st_check(ip->type == I_DIR, "namei '/' is a directory"); inode_put(ip); ip = 0; }

    // "/bin" must exist and be a directory.
    rc = namei("/bin", &ip);
    st_check(rc == 0, "namei '/bin' returns 0");
    if (ip) { st_check(ip->type == I_DIR, "namei '/bin' is a directory"); inode_put(ip); ip = 0; }

    // "/bin/init" must be a regular file.
    rc = namei("/bin/init", &ip);
    st_check(rc == 0, "namei '/bin/init' returns 0");
    if (ip) {
        st_check(ip->type == I_REG, "namei '/bin/init' is a regular file");
        st_check(ip->size > 0,     "namei '/bin/init' size > 0");
        inode_put(ip); ip = 0;
    }

    // Non-existent path.
    rc = namei("/does/not/exist", &ip);
    st_check(rc < 0, "namei '/does/not/exist' returns error");

    // File used as directory component.
    rc = namei("/bin/init/oops", &ip);
    st_check(rc < 0, "namei '/bin/init/oops' returns error (not a dir)");

    // /dev/console should cross the mount boundary and be I_CHR.
    rc = namei("/dev/console", &ip);
    st_check(rc == 0, "namei '/dev/console' returns 0");
    if (ip) {
        st_check(ip->type == I_CHR, "namei '/dev/console' is I_CHR");
        inode_put(ip); ip = 0;
    }
}

// ----------------------------------------------------------------------------
// tarfs directory tree test
// ----------------------------------------------------------------------------

static void test_tarfs_inode_tree(void) {
    printk("[SELFTEST] -- tarfs inode tree --\n");

    struct inode *bin = 0;
    int rc = namei("/bin", &bin);
    st_check(rc == 0 && bin != 0, "tarfs_tree: /bin resolves");
    if (!bin) return;

    // Count children of /bin.
    char buf[1024];
    uint64_t next = 0;
    int total = 0;
    while (1) {
        uint64_t n = next;
        int r = bin->ops->getdents(bin, n, buf, sizeof(buf), &next);
        if (r <= 0) break;
        // Walk records.
        int off = 0;
        while (off < r) {
            struct dirent64 *de = (struct dirent64 *)(buf + off);
            total++;
            off += de->d_reclen;
        }
    }
    st_check(total > 0, "tarfs_tree: /bin has entries");
    printk("[SELFTEST]   /bin entry count: %d\n", total);
    inode_put(bin);
}

// ----------------------------------------------------------------------------
// VFS edge-case tests — ".", "..", double-slash, trailing slash, dotdot
// ----------------------------------------------------------------------------

static void test_vfs_edge_cases(void) {
    printk("[SELFTEST] -- vfs edge cases --\n");
    struct inode *ip = 0;
    int rc;

    // /bin/. resolves to /bin (I_DIR).
    rc = namei("/bin/.", &ip);
    st_check(rc == 0, "namei /bin/. returns 0");
    if (ip) { st_check(ip->type == I_DIR, "namei /bin/. is I_DIR"); inode_put(ip); ip = 0; }

    // /bin/.. resolves to root (I_DIR with "bin" child).
    rc = namei("/bin/..", &ip);
    st_check(rc == 0, "namei /bin/.. returns 0");
    if (ip) {
        st_check(ip->type == I_DIR, "namei /bin/.. is I_DIR");
        struct inode *child = 0;
        int rc2 = ip->ops->lookup(ip, "bin", &child);
        st_check(rc2 == 0, "namei /bin/.. parent has 'bin' child");
        if (child) inode_put(child);
        inode_put(ip); ip = 0;
    }

    // Double slash: //bin/init resolves to /bin/init (I_REG).
    rc = namei("//bin/init", &ip);
    st_check(rc == 0, "namei //bin/init returns 0");
    if (ip) { st_check(ip->type == I_REG, "namei //bin/init is I_REG"); inode_put(ip); ip = 0; }

    // Dotdot crossing directories: /bin/../etc/rc resolves to /etc/rc.
    rc = namei("/bin/../etc/rc", &ip);
    st_check(rc == 0, "namei /bin/../etc/rc returns 0");
    if (ip) {
        st_check(ip->type == I_REG, "namei /bin/../etc/rc is I_REG");
        st_check(ip->size > 0,      "namei /bin/../etc/rc size > 0");
        inode_put(ip); ip = 0;
    }

    // Trailing slash on directory: /bin/ resolves to /bin.
    rc = namei("/bin/", &ip);
    st_check(rc == 0, "namei /bin/ returns 0");
    if (ip) { st_check(ip->type == I_DIR, "namei /bin/ is I_DIR"); inode_put(ip); ip = 0; }
}

// ----------------------------------------------------------------------------
// File read / seek tests — exercises fileread + fileseek via struct file
// ----------------------------------------------------------------------------

static void test_file_read(void) {
    printk("[SELFTEST] -- file read/seek --\n");

    struct inode *ip = 0;
    int rc = namei("/etc/rc", &ip);
    st_check(rc == 0 && ip != 0, "file_read: namei /etc/rc");
    if (!ip) return;

    struct file *f = filealloc();
    st_check(f != 0, "file_read: filealloc");
    if (!f) { inode_put(ip); return; }

    f->type     = FD_INODE;
    f->ip       = ip;       // namei already bumped refcnt
    f->off      = 0;
    f->readable = 1;
    f->writable = 0;

    // Read first 3 bytes; offset must advance.
    char buf[32];
    int n = fileread(f, buf, 3);
    st_check(n == 3,      "file_read: read 3 bytes returns 3");
    st_check(f->off == 3, "file_read: offset advanced to 3");

    // SEEK_CUR+0: query position.
    int pos = fileseek(f, 0, SEEK_CUR);
    st_check(pos == 3, "file_read: SEEK_CUR+0 == 3");

    // SEEK_END+0: position equals file size.
    int fsize = fileseek(f, 0, SEEK_END);
    st_check(fsize >= 0 && (unsigned int)fsize == ip->size,
             "file_read: SEEK_END+0 equals ip->size");

    // Read at EOF returns 0.
    n = fileread(f, buf, 1);
    st_check(n == 0, "file_read: read at EOF returns 0");

    // SEEK_SET to 0; re-read first 3 bytes.
    pos = fileseek(f, 0, SEEK_SET);
    st_check(pos == 0, "file_read: SEEK_SET to 0");
    n = fileread(f, buf, 3);
    st_check(n == 3, "file_read: re-read 3 bytes after seek-to-0");

    // tarfs is read-only; write must return an error.
    f->writable = 1;
    int w = filewrite(f, "x", 1);
    st_check(w < 0, "file_read: write to tarfs returns error (EROFS)");

    fileclose(f);   // decrements refcnt on ip; tarfs inode stays alive (static pool)
    st_check(1, "file_read: fileclose without crash");
}

// ----------------------------------------------------------------------------
// Chunked getdents test — iterate /bin one entry at a time vs. bulk
// ----------------------------------------------------------------------------

static void test_getdents_chunked(void) {
    printk("[SELFTEST] -- getdents chunked --\n");

    struct inode *bin = 0;
    int rc = namei("/bin", &bin);
    st_check(rc == 0 && bin != 0, "getdents_chunked: /bin resolves");
    if (!bin) return;

    // Count entries with a large buffer.
    char bigbuf[1024];
    uint64_t next = 0;
    int total_big = 0;
    while (1) {
        uint64_t prev = next;
        int r = bin->ops->getdents(bin, prev, bigbuf, sizeof(bigbuf), &next);
        if (r <= 0) break;
        int off = 0;
        while (off < r) {
            struct dirent64 *de = (struct dirent64 *)(bigbuf + off);
            total_big++;
            off += de->d_reclen;
        }
    }

    // Count the same entries one at a time using a 64-byte buffer.
    // All binary names in /bin are short enough that reclen <= 40 bytes.
    char smallbuf[64];
    next = 0;
    int total_small = 0;
    while (1) {
        uint64_t prev = next;
        int r = bin->ops->getdents(bin, prev, smallbuf, sizeof(smallbuf), &next);
        if (r <= 0 || next == prev) break;   // no progress or no entries
        int off = 0;
        while (off < r) {
            struct dirent64 *de = (struct dirent64 *)(smallbuf + off);
            total_small++;
            off += de->d_reclen;
        }
    }

    st_check(total_big > 0,            "getdents_chunked: big-buf count > 0");
    st_check(total_small == total_big, "getdents_chunked: small-buf count == big-buf count");
    printk("[SELFTEST]   getdents /bin: big=%d small=%d\n", total_big, total_small);

    inode_put(bin);
}

// ----------------------------------------------------------------------------
// sbfs + bio unit tests (run after sbfs_mount / binit)
// ----------------------------------------------------------------------------

static void test_bio_basic(void) {
    printk("[SELFTEST] -- bio cache basic --\n");

    // bread block 1 (superblock), verify it is non-zero (magic)
    struct buf *b = bread(1);
    st_check(b != 0, "bio: bread block 1 returns non-null");
    st_check(b->valid == 1, "bio: buffer is valid after bread");
    uint32_t magic = *(uint32_t *)b->data;
    st_check(magic == 0x53425631u, "bio: block 1 has sbfs magic 'SBV1'");
    brelse(b);

    // Re-bread same block — should hit the cache (same pointer from LRU)
    struct buf *b2 = bread(1);
    st_check(b2 != 0, "bio: second bread block 1 non-null");
    st_check(*(uint32_t *)b2->data == 0x53425631u, "bio: cache hit returns same data");
    brelse(b2);

    // bread 32 distinct blocks — cache should handle without panic
    for (int i = 0; i < 32; i++) {
        struct buf *bi = bread(i + 2);   // blocks 2..33 (log area)
        st_check(bi != 0, "bio: fill cache slot");
        brelse(bi);
    }
    st_check(1, "bio: filled 32 cache slots without panic");
}

static void test_sbfs_namei(void) {
    printk("[SELFTEST] -- sbfs namei /data --\n");

    struct inode *ip = 0;
    int rc = namei("/data", &ip);
    st_check(rc == 0,        "namei '/data' returns 0");
    st_check(ip != 0,        "namei '/data' non-null");
    if (ip) {
        st_check(ip->type == I_DIR, "namei '/data' is I_DIR");
        inode_put(ip); ip = 0;
    }

    // /data itself must be writable (sbfs ops have write != NULL)
    rc = namei("/data", &ip);
    if (ip) {
        st_check(ip->ops != 0 && ip->ops->write != 0,
                 "sbfs: /data inode has write op (not EROFS)");
        inode_put(ip);
    }
}

static void test_sbfs_rw(void) {
    printk("[SELFTEST] -- sbfs read/write --\n");

    // Open the root data inode and do a simple directory listing
    struct inode *root = 0;
    if (namei("/data", &root) < 0 || !root) {
        st_check(0, "sbfs_rw: /data not available");
        return;
    }

    // Create a file and write to it
    begin_op();
    struct inode *ip = sbfs_create(root, "st_rw.txt", 1);
    st_check(ip != 0, "sbfs: sbfs_create file");
    if (ip) {
        int w = sbfs_writei(ip, 0, "abcde", 5);
        st_check(w == 5, "sbfs: writei 5 bytes");
        end_op();

        char rbuf[8] = {0};
        int r = sbfs_readi(ip, 0, rbuf, 5);
        st_check(r == 5, "sbfs: readi 5 bytes");
        st_check(rbuf[0]=='a' && rbuf[4]=='e', "sbfs: data round-trips correctly");

        // Unlink the test file
        begin_op();
        int rc = sbfs_unlink(root, "st_rw.txt");
        st_check(rc == 0, "sbfs: unlink st_rw.txt");
        end_op();
        inode_put(ip);
    } else {
        end_op();
    }
    inode_put(root);
}

// ----------------------------------------------------------------------------
// Pipe kernel tests
// ----------------------------------------------------------------------------

static void test_pipe_basic(void) {
    printk("[SELFTEST] -- pipe basic --\n");

    struct file *rf = 0, *wf = 0;
    int rc = pipe_alloc(&rf, &wf);
    st_check(rc == 0, "pipe: alloc returns 0");
    st_check(rf != 0 && wf != 0, "pipe: file pointers non-null");
    st_check(rf->type == FD_PIPE, "pipe: read end is FD_PIPE");
    st_check(wf->type == FD_PIPE, "pipe: write end is FD_PIPE");
    st_check(rf->readable == 1 && rf->writable == 0, "pipe: read end perms");
    st_check(wf->readable == 0 && wf->writable == 1, "pipe: write end perms");

    // Write 10 bytes and read them back.
    pipe_write(wf->pipe, "0123456789", 10);
    char buf[16] = {0};
    int r = pipe_read(rf->pipe, buf, 10);
    st_check(r == 10, "pipe: read returns 10");
    st_check(buf[0] == '0' && buf[9] == '9', "pipe: data matches");

    fileclose(rf);
    fileclose(wf);
}

static void test_pipe_close_write_eof(void) {
    printk("[SELFTEST] -- pipe close-write EOF --\n");

    struct file *rf = 0, *wf = 0;
    pipe_alloc(&rf, &wf);

    pipe_write(wf->pipe, "abc", 3);
    fileclose(wf);

    char buf[8];
    int r = pipe_read(rf->pipe, buf, 8);
    st_check(r == 3, "pipe_eof: read returns remaining 3 bytes");

    r = pipe_read(rf->pipe, buf, 8);
    st_check(r == 0, "pipe_eof: second read returns 0 (EOF)");

    fileclose(rf);
}

static void test_pipe_alloc_free_cycle(void) {
    printk("[SELFTEST] -- pipe alloc/free cycle --\n");

    for (int i = 0; i < 50; i++) {
        struct file *rf = 0, *wf = 0;
        int rc = pipe_alloc(&rf, &wf);
        st_check(rc == 0, "pipe_cycle: alloc");
        fileclose(rf);
        fileclose(wf);
    }
    st_check(1, "pipe_cycle: 50 alloc/free cycles ok");
}

// ----------------------------------------------------------------------------
// Page refcount tests
// ----------------------------------------------------------------------------

static void test_page_refcount(void) {
    printk("[SELFTEST] -- page_refcount --\n");
    void *p = page_alloc();
    st_check(p != 0, "refcount: alloc succeeds");
    unsigned long pa = virt_to_phys((unsigned long)p);
    st_check(page_ref_get(pa) == 1, "refcount: new page has ref=1");
    page_get(pa);
    st_check(page_ref_get(pa) == 2, "refcount: after page_get ref=2");
    page_put(pa);
    st_check(page_ref_get(pa) == 1, "refcount: after page_put ref=1");
    page_put(pa);
    st_check(page_ref_get(pa) == 0, "refcount: after second page_put ref=0 (freed)");
}

// ----------------------------------------------------------------------------
// VMA tests
// ----------------------------------------------------------------------------

static void test_vma_pool(void) {
    printk("[SELFTEST] -- vma_pool --\n");
    struct vma *arr[10];
    for (int i = 0; i < 10; i++) {
        arr[i] = vma_alloc();
        st_check(arr[i] != 0, "vma_alloc succeeds");
    }
    for (int i = 0; i < 10; i++)
        vma_free(arr[i]);
    struct vma *v = vma_alloc();
    st_check(v != 0, "vma_alloc after free succeeds (no leak)");
    vma_free(v);
}

static void test_vma_insert_find(void) {
    printk("[SELFTEST] -- vma_insert_find --\n");
    struct vma *list = 0;
    struct vma *arr[10];
    for (int i = 0; i < 10; i++) {
        arr[i] = vma_alloc();
        arr[i]->start = (uint64_t)(i * 2) * PAGE_SIZE;
        arr[i]->end   = arr[i]->start + PAGE_SIZE;
        arr[i]->prot  = VMA_PROT_R;
        arr[i]->type  = VMA_TYPE_ANON;
        vma_insert(&list, arr[i]);
    }
    for (int i = 0; i < 10; i++) {
        uint64_t va = (uint64_t)(i * 2) * PAGE_SIZE;
        struct vma *found = vma_find(list, va);
        st_check(found == arr[i], "vma_find returns correct VMA");
    }
    // Verify sorted order
    int sorted = 1;
    for (struct vma *v = list; v && v->next; v = v->next) {
        if (v->start >= v->next->start) { sorted = 0; break; }
    }
    st_check(sorted, "vma_insert maintains sorted order");
    vma_list_free(&list);
    st_check(list == 0, "vma_list_free clears list");
}

static void test_vma_find_miss(void) {
    printk("[SELFTEST] -- vma_find_miss --\n");
    struct vma *list = 0;
    struct vma *v = vma_alloc();
    v->start = 0x1000;
    v->end   = 0x2000;
    v->prot  = VMA_PROT_R;
    v->type  = VMA_TYPE_ANON;
    vma_insert(&list, v);
    st_check(vma_find(list, 0x0500) == 0, "vma_find below range returns NULL");
    st_check(vma_find(list, 0x2000) == 0, "vma_find at end returns NULL");
    st_check(vma_find(list, 0x3000) == 0, "vma_find above range returns NULL");
    st_check(vma_find(list, 0x1000) == v, "vma_find at start returns VMA");
    st_check(vma_find(list, 0x1FFF) == v, "vma_find at last byte returns VMA");
    vma_list_free(&list);
}

static void test_vma_list_dup(void) {
    printk("[SELFTEST] -- vma_list_dup --\n");
    struct vma *list = 0;
    for (int i = 0; i < 5; i++) {
        struct vma *v = vma_alloc();
        v->start = (uint64_t)(i * 2) * PAGE_SIZE;
        v->end   = v->start + PAGE_SIZE;
        v->prot  = VMA_PROT_R | VMA_PROT_W;
        v->type  = VMA_TYPE_ANON;
        vma_insert(&list, v);
    }
    struct vma *dup = vma_list_dup(list);
    st_check(dup != 0, "vma_list_dup returns non-null");
    int count_orig = 0, count_dup = 0;
    for (struct vma *v = list; v; v = v->next) count_orig++;
    for (struct vma *v = dup;  v; v = v->next) count_dup++;
    st_check(count_orig == count_dup, "vma_list_dup has same count");
    struct vma *o = list, *d = dup;
    int match = 1;
    while (o && d) {
        if (o->start != d->start || o->end != d->end || o == d) { match = 0; break; }
        o = o->next; d = d->next;
    }
    st_check(match, "vma_list_dup copies are independent with same ranges");
    vma_list_free(&list);
    vma_list_free(&dup);
}

static void test_vma_remove(void) {
    printk("[SELFTEST] -- vma_remove --\n");
    struct vma *list = 0;
    struct vma *arr[5];
    for (int i = 0; i < 5; i++) {
        arr[i] = vma_alloc();
        arr[i]->start = (uint64_t)(i * 2) * PAGE_SIZE;
        arr[i]->end   = arr[i]->start + PAGE_SIZE;
        arr[i]->prot  = VMA_PROT_R;
        arr[i]->type  = VMA_TYPE_ANON;
        vma_insert(&list, arr[i]);
    }
    vma_remove(&list, arr[2]);
    st_check(vma_find(list, arr[2]->start) == 0, "removed VMA not found");
    int count = 0;
    for (struct vma *v = list; v; v = v->next) count++;
    st_check(count == 4, "list has 4 VMAs after remove");
    vma_free(arr[2]);
    vma_list_free(&list);
}

static void test_vma_split(void) {
    printk("[SELFTEST] -- vma_split --\n");

    // Split in the middle
    struct vma *list = 0;
    struct vma *v = vma_alloc();
    v->start = 0x10000;
    v->end   = 0x30000;
    v->prot  = VMA_PROT_R | VMA_PROT_W;
    v->type  = VMA_TYPE_ANON;
    vma_insert(&list, v);

    int rc = vma_split(&list, v, 0x18000, 0x20000);
    st_check(rc == 0, "vma_split: middle split returns 0");
    int count = 0;
    for (struct vma *w = list; w; w = w->next) count++;
    st_check(count == 2, "vma_split: middle split produces 2 VMAs");
    st_check(vma_find(list, 0x10000) != 0, "vma_split: left part present");
    st_check(vma_find(list, 0x1C000) == 0, "vma_split: middle gone");
    st_check(vma_find(list, 0x20000) != 0, "vma_split: right part present");
    vma_list_free(&list);

    // Split at left edge
    v = vma_alloc();
    v->start = 0x10000;
    v->end   = 0x20000;
    v->prot  = VMA_PROT_R;
    v->type  = VMA_TYPE_ANON;
    list = 0;
    vma_insert(&list, v);
    rc = vma_split(&list, v, 0x10000, 0x14000);
    st_check(rc == 0, "vma_split: left edge returns 0");
    st_check(list != 0, "vma_split: list non-null");
    st_check(list->start == 0x14000, "vma_split: left trimmed start correct");
    vma_list_free(&list);

    // Exact match (removes entirely)
    v = vma_alloc();
    v->start = 0x10000;
    v->end   = 0x20000;
    v->prot  = VMA_PROT_R;
    v->type  = VMA_TYPE_ANON;
    list = 0;
    vma_insert(&list, v);
    rc = vma_split(&list, v, 0x10000, 0x20000);
    st_check(rc == 0, "vma_split: exact match returns 0");
    st_check(list == 0, "vma_split: exact match removes VMA");
}

// ----------------------------------------------------------------------------
// Phase 8 selftests
// ----------------------------------------------------------------------------

static void test_signal_defaults(void) {
    printk("[SELFTEST] -- signal defaults --\n");

    struct pcb *p = alloc_proc();
    st_check(p != 0, "signal_defaults: alloc proc");
    if (!p) return;

    st_check(p->sig_pending == 0, "signal_defaults: pending is zero");
    st_check(p->sig_blocked == 0, "signal_defaults: blocked is zero");
    st_check(p->sig_saved_mask == 0, "signal_defaults: saved mask is zero");
    st_check(p->sig_handlers[SIGTERM].sa_handler == SIG_DFL,
             "signal_defaults: SIGTERM handler is SIG_DFL");
    st_check(p->sig_handlers[SIGCHLD].sa_handler == SIG_DFL,
             "signal_defaults: SIGCHLD handler is SIG_DFL");

    free_proc(p);
}

static void test_signal_pending_bitops(void) {
    printk("[SELFTEST] -- signal pending bitops --\n");
    sigset_t pending = 0;
    sigset_t blocked = 0;

    pending |= (1ULL << SIGTERM);
    st_check(sig_has_pending(pending, blocked), "signal_bitops: deliverable when unblocked");

    blocked |= (1ULL << SIGTERM);
    st_check(!sig_has_pending(pending, blocked), "signal_bitops: masked when blocked");

    blocked &= ~(1ULL << SIGTERM);
    st_check(sig_has_pending(pending, blocked), "signal_bitops: deliverable again after unblock");
}

static void test_termios_defaults(void) {
    printk("[SELFTEST] -- termios defaults --\n");

    struct termios t;
    termios_get(&t);
    st_check((t.c_lflag & (ISIG | ICANON | ECHO)) == (ISIG | ICANON | ECHO),
             "termios_defaults: lflag has ISIG|ICANON|ECHO");
    st_check((t.c_iflag & ICRNL) != 0, "termios_defaults: ICRNL set");
    st_check(t.c_cc[VINTR] == 0x03, "termios_defaults: VINTR is Ctrl-C");
    st_check(t.c_cc[VEOF] == 0x04, "termios_defaults: VEOF is Ctrl-D");
}

static void test_time_monotonic_basic(void) {
    printk("[SELFTEST] -- time monotonic basic --\n");
    uint64_t t1 = timer_ticks();
    uint64_t t2 = timer_ticks();
    st_check(t2 >= t1, "time_monotonic: timer_ticks non-decreasing");
}

// ----------------------------------------------------------------------------
// Page cache tests
// ----------------------------------------------------------------------------

static void test_pcache_basic(void) {
    printk("[SELFTEST] -- pcache basic --\n");

    /* Static so the slot's retained ip pointer doesn't alias a future
     * stack frame and produce a phantom cache hit in a later test. */
    static struct inode dummy_a, dummy_b;
    dummy_a = (struct inode){0};
    dummy_b = (struct inode){0};
    struct pcache_page *p1, *p2, *p3;

    int rc = pcache_get(&dummy_a, 0, &p1);
    st_check(rc == 0, "pcache_basic: first get returns 0");
    if (rc < 0) return;

    rc = pcache_get(&dummy_a, 0, &p2);
    st_check(rc == 0, "pcache_basic: second get (same key) returns 0");
    if (rc < 0) { pcache_put(p1); return; }

    st_check(p1 == p2,        "pcache_basic: same key returns same slot");
    st_check(p1->refcnt == 2, "pcache_basic: refcnt == 2 after two gets");

    pcache_put(p1);
    pcache_put(p2);

    rc = pcache_get(&dummy_b, 0, &p3);
    st_check(rc == 0, "pcache_basic: get with different inode returns 0");
    if (rc < 0) return;

    st_check(p3 != p1, "pcache_basic: different inode gets different slot");
    pcache_put(p3);

    /* Drop slot tracking for the dummy keys so subsequent tests that
     * happen to address-alias a real inode don't observe a stale hit. */
    pcache_flush_inode(&dummy_a);
    pcache_flush_inode(&dummy_b);
}

static void test_pcache_evict(void) {
    printk("\n[SELFTEST] -- pcache evict --\n");
    /* Use NSLOTS+1 distinct dummy keys; release each immediately so
     * eviction can recycle. */
    static struct inode dummies[PCACHE_NSLOTS + 1];
    for (int i = 0; i < PCACHE_NSLOTS + 1; i++)
        dummies[i] = (struct inode){0};
    int ok = 1;
    for (int i = 0; i < PCACHE_NSLOTS + 1; i++) {
        struct pcache_page *p;
        if (pcache_get(&dummies[i], 0, &p) < 0) { ok = 0; break; }
        pcache_put(p);
    }
    st_check(ok, "pcache_evict: NSLOTS+1 distinct gets all succeed");
    /* Touch first dummy: should still resolve (eviction may have reaped
     * an earlier slot, but pcache_get will simply re-fetch via miss path). */
    struct pcache_page *p;
    int rc = pcache_get(&dummies[0], 0, &p);
    st_check(rc == 0, "pcache_evict: re-get after eviction succeeds");
    if (rc == 0) pcache_put(p);
}

// ----------------------------------------------------------------------------
// Entry point
// ----------------------------------------------------------------------------

void selftest_run(void) {
    printk("========================================\n");
    printk("[SELFTEST] Kernel self-tests starting\n");
    printk("========================================\n");

    test_tarfs();
    test_alloc_free_proc();
    test_load_elf();
    test_uvmcow_share();
    test_leak_spawn_free();
    test_namei();
    test_tarfs_inode_tree();
    test_vfs_edge_cases();
    test_file_read();
    test_getdents_chunked();
    test_bio_basic();
    test_sbfs_namei();
    test_sbfs_rw();
    test_pipe_basic();
    test_pipe_close_write_eof();
    test_pipe_alloc_free_cycle();
    test_page_refcount();
    test_vma_pool();
    test_vma_insert_find();
    test_vma_find_miss();
    test_vma_list_dup();
    test_vma_remove();
    test_vma_split();
    test_signal_defaults();
    test_signal_pending_bitops();
    test_termios_defaults();
    test_time_monotonic_basic();
    test_pcache_basic();
    test_pcache_evict();

    printk("========================================\n");
    printk("[SELFTEST] Results: %d passed, %d failed\n", st_pass, st_fails);
    printk("========================================\n");

    if (st_fails > 0)
        printk("[SELFTEST] WARNING: %d self-test(s) FAILED\n", st_fails);
}
