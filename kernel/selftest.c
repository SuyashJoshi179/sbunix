#include <exec.h>
#include <file.h>
#include <inode.h>
#include <pmem.h>
#include <printk.h>
#include <proc.h>
#include <selftest.h>
#include <stat.h>
#include <tarfs.h>
#include <vfs.h>
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
    int rc = load_user_elf(pt, img, sz, &entry);
    st_check(rc == 0,                    "load_elf: load_user_elf returns 0");
    st_check(entry >= USER_TEXT_BASE,    "load_elf: entry >= USER_TEXT_BASE");
    st_check(entry <  KVMEM_OFFSET,      "load_elf: entry in user virtual space");

    free_user_pgtable(pt);
    st_check(1, "load_elf: free_user_pgtable without crash");
}

// ----------------------------------------------------------------------------
// uvmcopy test — copy a page table and verify it is a distinct object
// ----------------------------------------------------------------------------

static void test_uvmcopy(void) {
    printk("[SELFTEST] -- uvmcopy --\n");

    unsigned long sz = 0;
    const void *img = tarfs_find("bin/init", &sz);
    if (!img) { st_check(0, "uvmcopy: tarfs setup"); return; }

    pgtable_t parent_pt = create_user_pgtable();
    if (!parent_pt) { st_check(0, "uvmcopy: parent create_user_pgtable"); return; }

    unsigned long entry = 0;
    if (load_user_elf(parent_pt, img, sz, &entry) < 0) {
        free_user_pgtable(parent_pt);
        st_check(0, "uvmcopy: parent load_user_elf");
        return;
    }
    if (map_stack(parent_pt) < 0) {
        free_user_pgtable(parent_pt);
        st_check(0, "uvmcopy: parent map_stack");
        return;
    }

    pgtable_t child_pt = uvmcopy(parent_pt);
    st_check(child_pt != 0,              "uvmcopy: returns non-null");
    st_check(child_pt != parent_pt,      "uvmcopy: distinct page table root");

    // Both page tables should map the same virtual entry; if a PTE is
    // present in the parent's user range, the child must have one too.
    if (child_pt) {
        pte_t *ppte = get_pte(parent_pt, entry, /*alloc=*/0);
        pte_t *cpte = get_pte(child_pt,  entry, /*alloc=*/0);
        st_check(ppte != 0, "uvmcopy: parent PTE at entry present");
        st_check(cpte != 0, "uvmcopy: child PTE at entry present");

        if (ppte && cpte) {
            unsigned long ppa = pte_to_phyaddr(*ppte);
            unsigned long cpa = pte_to_phyaddr(*cpte);
            st_check(ppa != cpa, "uvmcopy: child page is a physical copy (different PA)");
        }

        free_user_pgtable(child_pt);
    }
    free_user_pgtable(parent_pt);
    st_check(1, "uvmcopy: cleanup without crash");
}

// ----------------------------------------------------------------------------
// Leak test: spawn + tear down 1000 processes and check pmem invariant
// ----------------------------------------------------------------------------

static void test_leak_spawn_free(void) {
    printk("[SELFTEST] -- leak: spawn/free 1000x --\n");

    unsigned long before = pmem_free_count();

    for (int i = 0; i < 1000; i++) {
        struct pcb *p = proc_spawn("bin/init");
        st_check(p != 0, "spawn bin/init");
        if (!p) continue;
        // Tear down the address space and proc manually (mirrors what
        // free_proc now does, exercising the full allocation/free path
        // without needing the scheduler running).
        free_proc(p);
    }

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
// Entry point
// ----------------------------------------------------------------------------

void selftest_run(void) {
    printk("========================================\n");
    printk("[SELFTEST] Kernel self-tests starting\n");
    printk("========================================\n");

    test_tarfs();
    test_alloc_free_proc();
    test_load_elf();
    test_uvmcopy();
    test_leak_spawn_free();
    test_namei();
    test_tarfs_inode_tree();
    test_vfs_edge_cases();
    test_file_read();
    test_getdents_chunked();

    printk("========================================\n");
    printk("[SELFTEST] Results: %d passed, %d failed\n", st_pass, st_fails);
    printk("========================================\n");

    if (st_fails > 0)
        printk("[SELFTEST] WARNING: %d self-test(s) FAILED\n", st_fails);
}
