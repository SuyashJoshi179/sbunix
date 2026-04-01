#include <proc.h>
#include <pmem.h>
#include <printk.h>
#include <riscv.h>
#include <string.h>
#include <tarfs.h>
#include <elf.h>

static struct pcb *procs;
static struct pcb *current;
static int next_pid = 1;
static struct context sched_context;

extern void enter_user(unsigned long satp, unsigned long sepc, unsigned long usp, unsigned long ksp);

static unsigned long page_round_down(unsigned long addr) {
    return addr & ~(PAGE_SIZE - 1);
}

static unsigned long min_ul(unsigned long a, unsigned long b) {
    return a < b ? a : b;
}

static unsigned long max_ul(unsigned long a, unsigned long b) {
    return a > b ? a : b;
}

static int copy_user_cstr(const char *src, char *dst, unsigned long max_len) {
    if (src == 0 || dst == 0 || max_len == 0) {
        return -1;
    }

    for (unsigned long i = 0; i < max_len; i++) {
        dst[i] = src[i];
        if (dst[i] == '\0') {
            return 0;
        }
    }

    return -1;
}

static int load_user_elf(pgtable_t pgtable, const char *img, unsigned long size, unsigned long *entry) {
    if (size < sizeof(elf64_ehdr_t)) {
        return -1;
    }

    const elf64_ehdr_t *eh = (const elf64_ehdr_t *)img;
    uint32_t magic = ((uint32_t)eh->e_ident[0]) |
                     ((uint32_t)eh->e_ident[1] << 8) |
                     ((uint32_t)eh->e_ident[2] << 16) |
                     ((uint32_t)eh->e_ident[3] << 24);

    if (magic != ELF_MAGIC ||
        eh->e_ident[4] != ELFCLASS64 ||
        eh->e_ident[5] != ELFDATA2LSB ||
        eh->e_type != ET_EXEC ||
        eh->e_machine != EM_RISCV ||
        eh->e_phentsize != sizeof(elf64_phdr_t)) {
        return -1;
    }

    if (eh->e_phoff + (unsigned long)eh->e_phnum * sizeof(elf64_phdr_t) > size) {
        return -1;
    }

    const elf64_phdr_t *ph = (const elf64_phdr_t *)(img + eh->e_phoff);
    for (unsigned long i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD) {
            continue;
        }

        if (ph[i].p_memsz < ph[i].p_filesz) {
            return -1;
        }

        if (ph[i].p_offset + ph[i].p_filesz > size) {
            return -1;
        }

        unsigned long seg_start = page_round_down((unsigned long)ph[i].p_vaddr);
        unsigned long seg_end = page_round_up((unsigned long)(ph[i].p_vaddr + ph[i].p_memsz));

        unsigned long perm = PTE_U;
        if (ph[i].p_flags & PF_R) perm |= PTE_R;
        if (ph[i].p_flags & PF_W) perm |= PTE_W;
        if (ph[i].p_flags & PF_X) perm |= PTE_X;

        for (unsigned long va = seg_start; va < seg_end; va += PAGE_SIZE) {
            void *page = page_alloc();
            if (page == 0) {
                return -1;
            }

            unsigned long file_start = (unsigned long)ph[i].p_vaddr;
            unsigned long file_end = (unsigned long)(ph[i].p_vaddr + ph[i].p_filesz);
            unsigned long copy_start = max_ul(va, file_start);
            unsigned long copy_end = min_ul(va + PAGE_SIZE, file_end);

            if (copy_end > copy_start) {
                unsigned long src_off = (unsigned long)ph[i].p_offset + (copy_start - file_start);
                unsigned long dst_off = copy_start - va;
                memmove((char *)page + dst_off, img + src_off, copy_end - copy_start);
            }

            vmem_map(pgtable, va, virt_to_phys((unsigned long)page), PAGE_SIZE, perm);
        }
    }

    *entry = (unsigned long)eh->e_entry;
    return 0;
}

// ----------------------------------------------------------------
// forkret — first-run entry point for new threads.
//
// When swtch first transfers control to a new thread, there is no
// prior sret to restore interrupts (SIE is cleared because we
// inherited the scheduler's interrupt-disabled state).  forkret
// enables interrupts and calls the thread's entry function.
// ----------------------------------------------------------------

static void forkret(void) {
    struct pcb *p = current;

    if (p->is_user) {
        uint64_t sstatus = read_sstatus();
        sstatus &= ~SSTATUS_SPP;   // return to U-mode
        sstatus |= SSTATUS_SPIE;   // enable interrupts in U-mode after sret
        write_sstatus(sstatus);
        enter_user(make_satp(p->pagetable), p->user_entry, p->user_sp,
               (unsigned long)p->kstack_page + KSTACK_SIZE);
        panic("enter_user returned unexpectedly");
    }

    write_sstatus(read_sstatus() | SSTATUS_SIE);
    p->entry();

    // If the thread function ever returns, mark it exited and
    // switch back to the scheduler permanently.
    p->state = PROC_ZOMBIE;
    swtch(&p->context, &sched_context);
    panic("zombie process resumed");
}

static int pte_is_leaf(pte_t pte) {
    return (pte & (PTE_R | PTE_W | PTE_X)) != 0;
}

static void free_user_pagetable_children(pgtable_t pgtable, int level) {
    for (int i = 0; i < 512; i++) {
        pte_t pte = pgtable[i];
        if ((pte & PTE_V) == 0) {
            continue;
        }

        unsigned long child_pa = pte_to_phyaddr(pte);
        void *child_kva = (void *)phys_to_virt(child_pa);

        if (pte_is_leaf(pte)) {
            page_free(child_kva);
            pgtable[i] = 0;
            continue;
        }

        if (level > 0) {
            free_user_pagetable_children((pgtable_t)child_kva, level - 1);
            page_free(child_kva);
            pgtable[i] = 0;
        }
    }
}

static void free_user_address_space(pgtable_t pgtable) {
    if (pgtable == 0) {
        return;
    }

    // Lower half (L2 indices 0..255) belongs to user space.
    for (int i = 0; i < 256; i++) {
        pte_t pte = pgtable[i];
        if ((pte & PTE_V) == 0) {
            continue;
        }

        unsigned long child_pa = pte_to_phyaddr(pte);
        pgtable_t child = (pgtable_t)phys_to_virt(child_pa);

        free_user_pagetable_children(child, 1);
        page_free(child);
        pgtable[i] = 0;
    }

    page_free(pgtable);
}

static int prepare_user_image(const char *img, unsigned long size,
                              pgtable_t *out_pgtable, unsigned long *out_entry,
                              unsigned long *out_user_sp) {
    pgtable_t pgtable = create_user_pgtable();
    if (pgtable == 0) {
        return -1;
    }

    if (load_user_elf(pgtable, img, size, out_entry) != 0) {
        free_user_address_space(pgtable);
        return -1;
    }

    *out_user_sp = map_stack(pgtable);
    if (*out_user_sp == 0) {
        free_user_address_space(pgtable);
        return -1;
    }

    *out_pgtable = pgtable;
    return 0;
}

static void reap_proc(struct pcb *victim) {
    struct pcb *prev = 0;
    struct pcb *p = procs;

    while (p && p != victim) {
        prev = p;
        p = p->next;
    }

    if (p == 0) {
        return;
    }

    if (prev) {
        prev->next = victim->next;
    } else {
        procs = victim->next;
    }

    if (victim->kstack_page) {
        page_free(victim->kstack_page);
        victim->kstack_page = 0;
    }

    if (victim->is_user && victim->pagetable) {
        free_user_address_space(victim->pagetable);
        victim->pagetable = 0;
    }

    page_free(victim);
}

static struct pcb *alloc_proc(void) {
    struct pcb *p = (struct pcb *)page_alloc();
    if (p == 0) {
        return 0;
    }
    memset(p, 0, PAGE_SIZE);

    p->kstack_page = page_alloc();
    if (p->kstack_page == 0) {
        page_free(p);
        return 0;
    }

    p->pid = next_pid++;
    p->state = PROC_UNUSED;

    if (procs == 0) {
        procs = p;
    } else {
        struct pcb *tail = procs;
        while (tail->next) {
            tail = tail->next;
        }
        tail->next = p;
    }

    return p;
}

static void proc_init_user(char *img, unsigned long size) {
    struct pcb *p = alloc_proc();
    if (p == 0) {
        panic("Failed to allocate user process");
    }

    p->state = PROC_READY;
    p->is_user = 1;
    p->entry = 0;

    if (prepare_user_image(img, size, &p->pagetable, &p->user_entry, &p->user_sp) != 0) {
        panic("Failed to load user ELF image");
    }

    memset(&p->context, 0, sizeof(struct context));
    p->context.sp = (uint64_t)p->kstack_page + KSTACK_SIZE;
    p->context.ra = (uint64_t)forkret;
}

static struct pcb *next_ready_proc(void) {
    if (procs == 0) {
        return 0;
    }

    struct pcb *start = current ? current->next : procs;
    if (start == 0) {
        start = procs;
    }

    struct pcb *p = start;
    do {
        if (p->state == PROC_READY) {
            return p;
        }
        p = p->next ? p->next : procs;
    } while (p != start);

    return 0;
}

// ----------------------------------------------------------------
// yield — give up CPU, switch back to scheduler
//
// Called from the timer interrupt handler (SIE already cleared by
// hardware) or voluntarily from a thread (SIE is enabled).  We
// save and restore the SIE bit so both paths work correctly:
//   - interrupt path: SIE stays cleared through scheduler, restored
//     by sret when we eventually return through trap_vector.
//   - voluntary path: SIE is cleared for the scheduler, then
//     re-enabled when this thread resumes.
// ----------------------------------------------------------------

void yield(void) {
    if (current == 0 || current->state != PROC_RUNNING)
        return;

    struct pcb *p = current;

    uint64_t sstatus = read_sstatus();
    write_sstatus(sstatus & ~SSTATUS_SIE);   // interrupts off

    p->state = PROC_READY;
    swtch(&p->context, &sched_context);

    // Resumed by the scheduler — restore previous interrupt state.
    write_sstatus(read_sstatus() | (sstatus & SSTATUS_SIE));
}

// ----------------------------------------------------------------
// scheduler loop — runs with interrupts disabled
// ----------------------------------------------------------------

static void scheduler_run(void) {
    while (1) {
        struct pcb *found = next_ready_proc();

        if (found == 0) {
            printk("scheduler: no runnable processes\n");
            while (1)
                asm volatile("wfi");
        }

        current = found;
        found->state = PROC_RUNNING;
        swtch(&sched_context, &found->context);

        if (current && current->state == PROC_ZOMBIE) {
            struct pcb *zombie = current;
            current = 0;
            reap_proc(zombie);
        }
    }
}

// ----------------------------------------------------------------
// sched_init — called once from boot(), never returns
// ----------------------------------------------------------------

void sched_init(void) {
    procs = 0;
    current = 0;

    struct tarfs_node init_prog;
    if (!tarfs_lookup("/bin/init", &init_prog)) {
        panic("Failed to find /bin/init in tarfs");
    }

    proc_init_user((char *)init_prog.data, init_prog.size);

    printk("scheduler: starting\n");
    scheduler_run();
}

void proc_exit_current(void) {
    if (current == 0 || current->state != PROC_RUNNING)
        return;

    struct pcb *p = current;
    p->state = PROC_ZOMBIE;
    swtch(&p->context, &sched_context);
    panic("exited process resumed");
}

int proc_exec_current(const char *path) {
    if (current == 0 || current->state != PROC_RUNNING || !current->is_user) {
        return -1;
    }

    char kpath[128];
    if (copy_user_cstr(path, kpath, sizeof(kpath)) != 0) {
        return -1;
    }

    struct tarfs_node node;
    if (!tarfs_lookup(kpath, &node)) {
        return -1;
    }

    pgtable_t new_pgtable = 0;
    unsigned long new_entry = 0;
    unsigned long new_user_sp = 0;
    if (prepare_user_image(node.data, node.size, &new_pgtable, &new_entry, &new_user_sp) != 0) {
        return -1;
    }

    pgtable_t old_pgtable = current->pagetable;

    current->pagetable = new_pgtable;
    current->user_entry = new_entry;
    current->user_sp = new_user_sp;
    memset(current->files, 0, sizeof(current->files));

    if (old_pgtable) {
        // We entered from U-mode with old user SATP active. Switch to kernel
        // page table before freeing old user root page table.
        write_satp(make_satp(kernel_pgtable));
        flush_tlb();
        free_user_address_space(old_pgtable);
    }

    return 0;
}

long proc_wait_current(void) {
    // Child relationships are not implemented yet (no fork/spawn model),
    // so wait has no reapable children to return.
    return -1;
}

int proc_open_current(const char *path) {
    if (current == 0 || current->state != PROC_RUNNING || !current->is_user) {
        return -1;
    }

    char kpath[128];
    if (copy_user_cstr(path, kpath, sizeof(kpath)) != 0) {
        return -1;
    }

    struct tarfs_node node;
    if (!tarfs_lookup(kpath, &node)) {
        return -1;
    }

    for (int fd = 3; fd < PROC_MAX_FILES; fd++) {
        if (!current->files[fd].used) {
            current->files[fd].used = 1;
            current->files[fd].data = node.data;
            current->files[fd].size = node.size;
            current->files[fd].off = 0;
            return fd;
        }
    }

    return -1;
}

long proc_read_current(int fd, void *buf, unsigned long len) {
    if (current == 0 || current->state != PROC_RUNNING || !current->is_user) {
        return -1;
    }

    if (fd < 3 || fd >= PROC_MAX_FILES || !current->files[fd].used || buf == 0) {
        return -1;
    }

    struct tarfs_node node;
    node.data = current->files[fd].data;
    node.size = current->files[fd].size;

    unsigned long n = tarfs_read(&node, current->files[fd].off, buf, len);
    current->files[fd].off += n;
    return (long)n;
}

int proc_close_current(int fd) {
    if (current == 0 || current->state != PROC_RUNNING || !current->is_user) {
        return -1;
    }

    if (fd < 3 || fd >= PROC_MAX_FILES || !current->files[fd].used) {
        return -1;
    }

    current->files[fd].used = 0;
    current->files[fd].data = 0;
    current->files[fd].size = 0;
    current->files[fd].off = 0;
    return 0;
}

pgtable_t proc_current_pagetable(void) {
    if (current == 0) {
        return 0;
    }
    return current->pagetable;
}

unsigned long proc_current_user_entry(void) {
    if (current == 0) {
        return 0;
    }
    return current->user_entry;
}

unsigned long proc_current_user_sp(void) {
    if (current == 0) {
        return 0;
    }
    return current->user_sp;
}

unsigned long proc_current_kstack_top(void) {
    if (current == 0 || current->kstack_page == 0) {
        return 0;
    }
    return (unsigned long)current->kstack_page + KSTACK_SIZE;
}

pgtable_t create_user_pgtable(void) {
    pgtable_t pgtable;
    pgtable = (pgtable_t) page_alloc();

    if(pgtable == 0) {
        panic("Physical memory allocation failed while creating user pgtable!");
    }

    // format all to zeros initially
    memset(pgtable, 0, PAGE_SIZE);

    // map kernel from the kernel page table to upper half
    for (int i = 256; i < 512; i++) {
        pgtable[i] = kernel_pgtable[i];
    }

    return pgtable;
}

void map_code(pgtable_t pgtable, char *data, unsigned long size) {
    unsigned long vaddr = 0;

    for(unsigned long offset = 0; offset < size; offset += PAGE_SIZE) {
        void *paddr = page_alloc();
        if(paddr == 0) {
            panic("Physical memory allocation failed while mapping code for process!");
        }
        unsigned long bytes = size - offset;
        if(bytes > PAGE_SIZE) bytes = PAGE_SIZE;
        memset(paddr, 0, PAGE_SIZE);
        memmove(paddr, data + offset, bytes);
        vmem_map(pgtable, vaddr+offset, virt_to_phys((unsigned long)paddr), PAGE_SIZE, PTE_U | PTE_R | PTE_W | PTE_X);
    }

}

unsigned long map_stack(pgtable_t pgtable) {
    void *phyaddr = page_alloc();
    unsigned long vaddr = USER_STACK_BASE - PAGE_SIZE;

    if(phyaddr == 0) return 0;

    vmem_map(pgtable, vaddr, virt_to_phys((unsigned long)phyaddr), PAGE_SIZE, PTE_U | PTE_R | PTE_W);
    return USER_STACK_BASE;
}
