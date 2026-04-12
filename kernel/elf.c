#include <elf.h>
#include <pmem.h>
#include <vmem.h>
#include <string.h>
#include <printk.h>

uint64_t elf_load(pgtable_t pgtable, char *img, unsigned long size) {
    if (size < sizeof(Elf64_Ehdr)) return 0;

    Elf64_Ehdr *eh = (Elf64_Ehdr *)img;

    /* Verify ELF magic */
    if (*((uint32_t *)eh->e_ident) != ELF_MAGIC) {
        printk("[elf] bad magic\n");
        return 0;
    }
    if (eh->e_type != ET_EXEC) {
        printk("[elf] not an executable\n");
        return 0;
    }

    /* Walk program headers */
    for (int i = 0; i < eh->e_phnum; i++) {
        Elf64_Phdr *ph = (Elf64_Phdr *)(img + eh->e_phoff
                                        + i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD) continue;
        if (ph->p_memsz == 0)      continue;

        /* Build PTE permission flags */
        unsigned long perm = PTE_U;
        if (ph->p_flags & PF_R) perm |= PTE_R;
        if (ph->p_flags & PF_W) perm |= PTE_W;
        if (ph->p_flags & PF_X) perm |= PTE_X;

        uint64_t vaddr = ph->p_vaddr;
        uint64_t memsz = ph->p_memsz;
        uint64_t filesz = ph->p_filesz;
        uint64_t off    = ph->p_offset;

        /* Map pages for this segment (page-aligned) */
        uint64_t va     = vaddr & ~(uint64_t)(PAGE_SIZE - 1);
        uint64_t va_end = (vaddr + memsz + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);

        for (uint64_t v = va; v < va_end; v += PAGE_SIZE) {
            void *page = page_alloc();
            if (page == 0) { printk("[elf] OOM\n"); return 0; }
            memset(page, 0, PAGE_SIZE);

            /* Copy file bytes that fall in this page */
            uint64_t page_start = v;
            uint64_t page_end   = v + PAGE_SIZE;

            /* file data range: [vaddr, vaddr+filesz) */
            uint64_t copy_start = (page_start > vaddr)       ? page_start : vaddr;
            uint64_t copy_end   = (page_end   < vaddr+filesz)? page_end   : vaddr+filesz;

            if (copy_start < copy_end) {
                uint64_t dst_off = copy_start - page_start;
                uint64_t src_off = off + (copy_start - vaddr);
                memmove((char *)page + dst_off,
                        img + src_off,
                        copy_end - copy_start);
            }

            vmem_map(pgtable, v,
                     virt_to_phys((unsigned long)page),
                     PAGE_SIZE, perm);
        }

        printk("[elf] loaded segment vaddr=%lx memsz=%ld perm=%lx\n",
               vaddr, memsz, perm);
    }

    return eh->e_entry;
}
