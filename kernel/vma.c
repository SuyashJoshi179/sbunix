#include <page_ref.h>
#include <pmem.h>
#include <printk.h>
#include <proc.h>
#include <string.h>
#include <vma.h>
#include <vmem.h>

#define VMA_POOL_SIZE 256

static struct vma  vma_pool[VMA_POOL_SIZE];
static struct vma *vma_freelist;
static int         vma_pool_inited;

static void vma_pool_init(void) {
    if (vma_pool_inited) return;
    vma_pool_inited = 1;
    vma_freelist = 0;
    for (int i = VMA_POOL_SIZE - 1; i >= 0; i--) {
        vma_pool[i].next = vma_freelist;
        vma_freelist = &vma_pool[i];
    }
}

struct vma *vma_alloc(void) {
    if (!vma_pool_inited) vma_pool_init();
    if (!vma_freelist) return 0;
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

void vma_insert(struct vma **list, struct vma *v) {
    struct vma **pp = list;
    while (*pp && (*pp)->start < v->start)
        pp = &(*pp)->next;
    if (*pp && (*pp)->start < v->end)
        panic("vma_insert: overlap");
    v->next = *pp;
    *pp = v;
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
        v->end = start;
        vma_insert(list, right);
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
        struct vma *sv = find_stack_vma(p->vma_list);
        uint64_t min_start = USER_STACK_TOP - (MAX_STACK_PAGES * PAGE_SIZE);
        if (sv && stval < sv->start && fault_va >= min_start) {
            uint64_t user_sp = trapframe[1];
            if (stval >= user_sp - PAGE_SIZE) {
                sv->start = fault_va;
                v = sv;
            } else {
                return -1;
            }
        } else {
            return -1;
        }
    }

    // Permission check
    if (scause == 15 && !(v->prot & VMA_PROT_W))
        return -1;
    if (scause == 12 && !(v->prot & VMA_PROT_X))
        return -1;

    // Check if PTE already exists
    pte_t *pte = get_pte(p->pagetable, fault_va, 0);
    if (pte && (*pte & PTE_V)) {
        if (scause == 15 && !(*pte & PTE_W) && (v->flags & VMA_FLAG_COW)) {
            unsigned long old_pa = pte_to_phyaddr(*pte);
            if (page_ref_get(old_pa) == 1) {
                *pte |= PTE_W;
                flush_tlb();
                return 0;
            }
            void *new_page = page_alloc();
            if (!new_page) return -1;
            memmove(new_page, (void *)phys_to_virt(old_pa), PAGE_SIZE);
            unsigned long new_pa = virt_to_phys((unsigned long)new_page);
            unsigned long perm = PTE_U | PTE_V | PTE_R | PTE_W;
            if (v->prot & VMA_PROT_X) perm |= PTE_X;
            *pte = phyaddr_to_pte(new_pa) | perm;
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
