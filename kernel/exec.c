#include <elf.h>
#include <exec.h>
#include <pmem.h>
#include <printk.h>
#include <proc.h>
#include <string.h>
#include <tarfs.h>
#include <vmem.h>

// ---------------------------------------------------------------------------
// load_user_elf — map an ELF binary into a user page table
// ---------------------------------------------------------------------------
// Each PT_LOAD segment is mapped at its specified p_vaddr.  File data is
// copied into freshly-allocated physical pages through their kernel virtual
// addresses, so the active SATP does not need to point at pt.
int load_user_elf(pgtable_t pt, const void *img, unsigned long img_size,
                  unsigned long *entry_out) {
    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)img;

    // Validate ELF header
    if (*(const uint32_t *)ehdr->e_ident != ELF_MAGIC) {
        printk("exec: bad ELF magic\n");
        return -1;
    }
    if (ehdr->e_ident[4] != ELF_CLASS64) {
        printk("exec: not 64-bit ELF\n");
        return -1;
    }
    if (ehdr->e_machine != EM_RISCV) {
        printk("exec: not RISC-V ELF\n");
        return -1;
    }

    const Elf64_Phdr *phdrs =
        (const Elf64_Phdr *)((const char *)img + ehdr->e_phoff);

    for (int i = 0; i < ehdr->e_phnum; i++) {
        const Elf64_Phdr *ph = &phdrs[i];
        if (ph->p_type != PT_LOAD) continue;
        if (ph->p_memsz == 0) continue;

        // Build PTE permission bits
        unsigned long perm = PTE_U;
        if (ph->p_flags & PF_R) perm |= PTE_R;
        if (ph->p_flags & PF_W) perm |= PTE_W;
        if (ph->p_flags & PF_X) perm |= PTE_X;

        // Page-aligned range to map
        unsigned long va_start = ph->p_vaddr & ~(PAGE_SIZE - 1);
        unsigned long va_end   = page_round_up(ph->p_vaddr + ph->p_memsz);

        for (unsigned long va = va_start; va < va_end; va += PAGE_SIZE) {
            // Allocate a zeroed physical page; page_alloc returns kernel virt addr
            void *kpage = page_alloc();
            if (!kpage) {
                printk("exec: OOM loading segment\n");
                return -1;
            }

            // Copy file data that falls within this page.
            // File data covers [p_vaddr, p_vaddr + p_filesz).
            unsigned long page_end = va + PAGE_SIZE;
            unsigned long copy_va_start = (ph->p_vaddr > va)         ? ph->p_vaddr : va;
            unsigned long copy_va_end   = (ph->p_vaddr + ph->p_filesz < page_end)
                                          ? ph->p_vaddr + ph->p_filesz : page_end;

            if (copy_va_start < copy_va_end) {
                unsigned long page_off = copy_va_start - va;
                unsigned long file_off = ph->p_offset + (copy_va_start - ph->p_vaddr);
                memmove((char *)kpage + page_off,
                        (const char *)img + file_off,
                        copy_va_end - copy_va_start);
            }
            // Bytes in [p_vaddr+p_filesz, p_vaddr+p_memsz) (BSS) stay zero.

            vmem_map(pt, va, virt_to_phys((unsigned long)kpage), PAGE_SIZE, perm);
        }
    }

    *entry_out = ehdr->e_entry;
    return 0;
}

// ---------------------------------------------------------------------------
// proc_spawn — create a PROC_READY user process from a path in tarfs
// ---------------------------------------------------------------------------
struct pcb *proc_spawn(const char *path) {
    struct pcb *p = alloc_proc();
    if (!p) return 0;

    p->pagetable = create_user_pgtable();
    if (!p->pagetable) {
        printk("proc_spawn: create_user_pgtable failed\n");
        free_proc(p);
        return 0;
    }

    unsigned long img_size;
    const void *img = tarfs_find(path, &img_size);
    if (!img) {
        printk("proc_spawn: '%s' not found in tarfs\n", path);
        free_user_pgtable(p->pagetable);
        free_proc(p);
        return 0;
    }

    unsigned long entry;
    if (load_user_elf(p->pagetable, img, img_size, &entry) < 0) {
        free_user_pgtable(p->pagetable);
        free_proc(p);
        return 0;
    }

    if (map_stack(p->pagetable) < 0) {
        printk("proc_spawn: map_stack failed\n");
        free_user_pgtable(p->pagetable);
        free_proc(p);
        return 0;
    }

    p->is_user    = 1;
    p->user_entry = entry;
    p->user_sp    = USER_STACK_TOP;
    p->state      = PROC_READY;

    printk("proc_spawn: spawned pid=%d from '%s', entry=0x%lx\n",
           p->pid, path, entry);
    return p;
}
