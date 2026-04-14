#include <exec.h>
#include <pmem.h>
#include <printk.h>
#include <proc.h>
#include <selftest.h>
#include <tarfs.h>
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

    printk("========================================\n");
    printk("[SELFTEST] Results: %d passed, %d failed\n", st_pass, st_fails);
    printk("========================================\n");

    if (st_fails > 0)
        printk("[SELFTEST] WARNING: %d self-test(s) FAILED\n", st_fails);
}
