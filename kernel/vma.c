#include <errno.h>
#include <inode.h>
#include <page_cache.h>
#include <page_ref.h>
#include <pmem.h>
#include <printk.h>
#include <proc.h>
#include <resource.h>
#include <string.h>
#include <vma.h>
#include <vmem.h>

/* Slab allocator: refill from kernel page allocator on demand. Each refill
 * page becomes (PAGE_SIZE / sizeof(struct vma)) slots threaded onto the
 * free list via v->next. Slots are reused indefinitely; slab pages are
 * never freed (acceptable for a course OS; future TODO: reclaim). */
static struct vma *vma_freelist;

static int vma_slab_refill(void) {
    void *page = page_alloc();
    if (!page) return -1;
    unsigned int n = PAGE_SIZE / sizeof(struct vma);
    struct vma *arr = (struct vma *)page;
    for (unsigned int i = 0; i < n; i++) {
        arr[i].next = vma_freelist;
        vma_freelist = &arr[i];
    }
    return 0;
}

struct vma *vma_alloc(void) {
    if (!vma_freelist && vma_slab_refill() < 0) return 0;
    struct vma *v = vma_freelist;
    vma_freelist = v->next;
    memset(v, 0, sizeof(*v));
    return v;
}

void vma_free(struct vma *v) {
    if (!v) return;
    memset(v, 0, sizeof(*v));
    v->next = vma_freelist;
    vma_freelist = v;
}

struct vma *vma_find(struct vma *list, uint64_t va) {
    for (struct vma *v = list; v; v = v->next)
        if (va >= v->start && va < v->end)
            return v;
    return 0;
}

int vma_insert(struct vma **list, struct vma *v) {
    struct vma **pp = list;
    struct vma  *prev = 0;
    while (*pp && (*pp)->start < v->start) {
        prev = *pp;
        pp = &(*pp)->next;
    }
    /* Reject overlap with the successor (slot at *pp) or with the
     * immediately preceding node. */
    if (*pp && (*pp)->start < v->end) return -EINVAL;
    if (prev && prev->end > v->start) return -EINVAL;
    v->next = *pp;
    *pp = v;
    return 0;
}

void vma_remove(struct vma **list, struct vma *v) {
    struct vma **pp = list;
    while (*pp && *pp != v)
        pp = &(*pp)->next;
    if (*pp) *pp = v->next;
    v->next = 0;
}

void vma_list_free(struct vma **list) {
    struct vma *v = *list;
    while (v) {
        struct vma *next = v->next;
        vma_free(v);
        v = next;
    }
    *list = 0;
}

struct vma *vma_list_dup(struct vma *src) {
    struct vma *head = 0;
    struct vma **pp = &head;
    for (struct vma *v = src; v; v = v->next) {
        struct vma *copy = vma_alloc();
        if (!copy) {
            vma_list_free(&head);
            return 0;
        }
        *copy = *v;
        copy->next = 0;
        /* File VMAs hold an inode ref balanced by inode_put in
         * vma_drop_file_pages. The child needs its own ref so both
         * parent and child can release independently on teardown. */
        if (copy->type == VMA_TYPE_FILE && copy->file) {
            inode_get(copy->file);
        }
        *pp = copy;
        pp = &copy->next;
    }
    return head;
}

// --- VMA split (for munmap of partial range) ---

int vma_split(struct vma **list, struct vma *v, uint64_t start, uint64_t end) {
    if (start <= v->start && end >= v->end) {
        vma_remove(list, v);
        vma_free(v);
        return 0;
    }
    if (start > v->start && end < v->end) {
        struct vma *right = vma_alloc();
        if (!right) return -1;
        right->start = end;
        right->end   = v->end;
        right->prot  = v->prot;
        right->type  = v->type;
        right->flags = v->flags;
        uint64_t saved_end = v->end;
        v->end = start;
        if (vma_insert(list, right) < 0) {
            v->end = saved_end;
            vma_free(right);
            return -1;
        }
        return 0;
    }
    if (start <= v->start) {
        v->start = end;
        return 0;
    }
    v->end = start;
    return 0;
}

// --- Page fault handler ---

static struct vma *find_stack_vma(struct vma *list) {
    for (struct vma *v = list; v; v = v->next)
        if (v->type == VMA_TYPE_STACK)
            return v;
    return 0;
}

int user_page_fault(uint64_t scause, uint64_t stval, uint64_t *trapframe) {
    struct pcb *p = current_proc();
    if (!p) return -1;

    uint64_t fault_va = stval & ~(PAGE_SIZE - 1);

    struct vma *v = vma_find(p->vma_list, stval);

    if (!v) {
        /* Stack auto-grow: extend the stack VMA downward as long as the
         * fault address is below it but above the rlimit-derived floor.
         * We deliberately do NOT gate on `stval >= user_sp - PAGE_SIZE`
         * — gcc -O0 and Rust routinely allocate large stack frames in
         * one bump and then probe deep into the new range; rejecting
         * those would SIGSEGV the program even though the fault is
         * within the rlimit. Linux does the same: any below-stack fault
         * inside the rlimit floor extends the stack. */
        struct vma *sv = find_stack_vma(p->vma_list);
        rlim_t stack_max = p->rlim[RLIMIT_STACK].rlim_cur;
        uint64_t stack_cap = USER_STACK_TOP - USER_TEXT_BASE;
        if (stack_max == RLIM_INFINITY || stack_max > stack_cap)
            stack_max = stack_cap;
        uint64_t min_start = USER_STACK_TOP - stack_max;
        if (sv && stval < sv->start && fault_va >= min_start) {
            sv->start = fault_va;
            v = sv;
        } else {
            return -1;
        }
    }

    // Permission check
    if (scause == 15 && !(v->prot & VMA_PROT_W))
        return -1;
    if (scause == 12 && !(v->prot & VMA_PROT_X))
        return -1;

    if (v->type == VMA_TYPE_FILE) {
        /* Bounds against file size: faulting past EOF → SIGBUS (return -1). */
        uint64_t mapping_off = (fault_va - v->start);
        uint64_t file_pos    = v->file_off + mapping_off;
        uint64_t file_pgidx  = file_pos / PAGE_SIZE;
        if (file_pos >= v->file->size) return -1;

        /* Already-present PTE: write-fault upgrade paths. */
        pte_t *pte_existing = get_pte(p->pagetable, fault_va, 0);
        if (pte_existing && (*pte_existing & PTE_V)) {
            if (scause != 15) return -1;          /* read fault on present? bug */
            unsigned long old_pa = pte_to_phyaddr(*pte_existing);

            if (v->flags & VMA_FLAG_COW) {
                /* MAP_PRIVATE writable: copy the current backing page
                 * to a fresh anon page and remap.
                 *
                 * The current backing can be either:
                 *   (a) the pcache slot for (file, file_pgidx) — first
                 *       write since the file was faulted in;
                 *   (b) an anon page from an earlier CoW that survived
                 *       a subsequent fork (uvmcow_share clears PTE_W
                 *       and bumps page_refs, so the next write here
                 *       takes this branch with old_pa = anon).
                 *
                 * Previous code unconditionally ran
                 *   pcache_get(...); pcache_put × 2;
                 * which is correct for (a) — drop the lookup ref plus
                 * this proc's fault-time pcache ref — but for (b) it
                 * decrements an unrelated slot's refcnt by 2, eventually
                 * underflowing it and silently corrupting whichever
                 * (ip, pgidx) that slot is caching for another proc.
                 * It also failed to release the anon refcnt this PTE
                 * held in case (b), leaking page_refs[anon_pfn] by 1
                 * per fork+CoW cycle. */
                struct pcache_page *opp = pcache_pa_to_slot(old_pa);
                void *np = page_alloc();
                if (!np) return -1;
                memmove(np, (void *)phys_to_virt(old_pa), PAGE_SIZE);
                unsigned long new_pa = virt_to_phys((unsigned long)np);
                /* Derive perms from v->prot so PROT-clearances aren't
                 * silently widened on upgrade. sys_mmap requires
                 * VMA_PROT_R whenever VMA_PROT_W is set (RISC-V W-only
                 * is reserved), so PTE_R will be present here. */
                unsigned long perm = PTE_U | PTE_V | PTE_W;
                if (v->prot & VMA_PROT_R) perm |= PTE_R;
                if (v->prot & VMA_PROT_X) perm |= PTE_X;
                *pte_existing =
                    phyaddr_to_pte(new_pa) | perm | PTE_LEAF_AD;
                if (opp) {
                    /* (a): drop this proc's fault-time pcache ref. */
                    pcache_put(opp);
                } else {
                    /* (b): drop the anon refcnt this PTE held. */
                    page_put(old_pa);
                }
                flush_tlb();
                return 0;
            }
            if (v->flags & VMA_FLAG_SHARED) {
                /* Upgrade RO → RW. Mark the cache page dirty so the
                 * msync/munmap flush path writes it back. */
                struct pcache_page *pp;
                if (pcache_get(v->file, file_pgidx, &pp) < 0) return -1;
                pp->dirty = 1;
                /* Drop the lookup ref; the long-lived fault-time ref still pins. */
                pcache_put(pp);

                /* Derive perms from v->prot; sys_mmap guarantees
                 * VMA_PROT_R when VMA_PROT_W is set. */
                unsigned long perm = PTE_U | PTE_V | PTE_W;
                if (v->prot & VMA_PROT_R) perm |= PTE_R;
                if (v->prot & VMA_PROT_X) perm |= PTE_X;
                unsigned long pa = pte_to_phyaddr(*pte_existing);
                *pte_existing =
                    phyaddr_to_pte(pa) | perm | PTE_LEAF_AD;
                flush_tlb();
                return 0;
            }
            return -1;
        }

        struct pcache_page *pp;
        int rc = pcache_get(v->file, file_pgidx, &pp);
        if (rc < 0) return -1;

        unsigned long perm = PTE_U | PTE_V;
        if (v->prot & VMA_PROT_R) perm |= PTE_R;
        if (v->prot & VMA_PROT_X) perm |= PTE_X;
        /* RO install: writable mappings handled by T10 / T11 via the
         * present-PTE branch above. We never install PTE_W here in T9. */
        unsigned long pa = virt_to_phys((unsigned long)pp->page);
        vmem_map(p->pagetable, fault_va, pa, PAGE_SIZE, perm);
        flush_tlb();
        /* refcnt remains held; released in vma teardown (Task 12). */
        return 0;
    }

    // Check if PTE already exists
    pte_t *pte = get_pte(p->pagetable, fault_va, 0);
    if (pte && (*pte & PTE_V)) {
        if (scause == 15 && !(*pte & PTE_W) && (v->flags & VMA_FLAG_COW)) {
            unsigned long old_pa = pte_to_phyaddr(*pte);
            if (page_ref_get(old_pa) == 1) {
                *pte |= PTE_W | PTE_D;
                flush_tlb();
                return 0;
            }
            void *new_page = page_alloc();
            if (!new_page) return -1;
            memmove(new_page, (void *)phys_to_virt(old_pa), PAGE_SIZE);
            unsigned long new_pa = virt_to_phys((unsigned long)new_page);
            unsigned long perm = PTE_U | PTE_V | PTE_R | PTE_W;
            if (v->prot & VMA_PROT_X) perm |= PTE_X;
            *pte = phyaddr_to_pte(new_pa) | perm | PTE_LEAF_AD;
            page_put(old_pa);
            flush_tlb();
            return 0;
        }
        return -1;
    }

    // Demand page: allocate zero page and map
    if (v->type == VMA_TYPE_HEAP && fault_va >= v->end)
        return -1;

    void *page = page_alloc();
    if (!page) return -1;

    unsigned long perm = PTE_U | PTE_V;
    if (v->prot & VMA_PROT_R) perm |= PTE_R;
    if (v->prot & VMA_PROT_W) perm |= PTE_W;
    if (v->prot & VMA_PROT_X) perm |= PTE_X;

    vmem_map(p->pagetable, fault_va, virt_to_phys((unsigned long)page),
             PAGE_SIZE, perm);
    flush_tlb();
    return 0;
}

/* For a VMA_TYPE_FILE vma, walk its faulted-in PTEs, flush dirty cache
 * pages (MAP_SHARED only), drop pcache refcnts, and clear PTEs. Caller
 * frees the VMA struct via vma_list_free or vma_split. */
void vma_drop_file_pages(struct pcb *p, struct vma *v) {
    if (v->type != VMA_TYPE_FILE) return;
    for (uint64_t va = v->start; va < v->end; va += PAGE_SIZE) {
        pte_t *pte = get_pte(p->pagetable, va, 0);
        if (!pte || !(*pte & PTE_V)) continue;
        unsigned long pa = pte_to_phyaddr(*pte);
        uint64_t pgidx = (va - v->start + v->file_off) / PAGE_SIZE;

        struct pcache_page *pp;
        if (pcache_get(v->file, pgidx, &pp) == 0) {
            unsigned long pcache_pa = virt_to_phys((unsigned long)pp->page);
            if (pcache_pa == pa) {
                /* PTE points at the cache page (T9 RO install or T11
                 * shared-RW upgrade). */
                if (pp->dirty && (v->flags & VMA_FLAG_SHARED)
                    && v->file->ops && v->file->ops->writepage_locked) {
                    v->file->ops->writepage_locked(v->file, pgidx,
                                                   pp->page);
                    pp->dirty = 0;
                }
                pcache_put(pp);   /* fault-time ref */
                pcache_put(pp);   /* lookup ref     */
            } else {
                /* PTE points at a CoW anon page (T10). Cache page is
                 * untouched by this PTE; just drop the lookup ref and
                 * free the anon page. */
                pcache_put(pp);   /* lookup ref only */
                page_put(pa);
            }
        }
        *pte = 0;
    }
    flush_tlb();
    if (v->file) { inode_put(v->file); v->file = 0; }
}

/* fork() inherits PTEs via uvmcow_share, which only bumps anon page_get
 * refs. For VMA_TYPE_FILE PTEs that point at pcache slots, the child
 * needs its own fault-time pcache ref so parent and child can release
 * independently. Walk the child's page table for this VMA; for each
 * inherited PTE that targets a pcache page, bump the slot's refcnt. */
int vma_dup_file_pages(struct pcb *child, struct vma *v) {
    if (v->type != VMA_TYPE_FILE || !v->file) return 0;
    for (uint64_t va = v->start; va < v->end; va += PAGE_SIZE) {
        pte_t *pte = get_pte(child->pagetable, va, 0);
        if (!pte || !(*pte & PTE_V)) continue;
        unsigned long pa = pte_to_phyaddr(*pte);
        uint64_t pgidx = (va - v->start + v->file_off) / PAGE_SIZE;
        struct pcache_page *pp;
        if (pcache_get(v->file, pgidx, &pp) < 0) return -1;
        unsigned long pcache_pa = virt_to_phys((unsigned long)pp->page);
        if (pcache_pa == pa) {
            /* The pcache_get bump itself becomes the child's fault-time
             * ref. Don't release it here. */
        } else {
            /* PTE points at a CoW anon page (write already taken in
             * parent before fork). uvmcow_share already bumped the anon
             * page_ref. Release the lookup ref we just acquired. */
            pcache_put(pp);
        }
    }
    return 0;
}
