#include <elf.h>
#include <errno.h>
#include <exec.h>
#include <file.h>
#include <inode.h>
#include <pmem.h>
#include <printk.h>
#include <proc.h>
#include <string.h>
#include <tarfs.h>
#include <vfs.h>
#include <vma.h>
#include <vmem.h>

// Declared in devfs.c.
struct inode *devfs_console_inode(void);

// ---------------------------------------------------------------------------
// load_user_elf — map an ELF binary into a user page table
// ---------------------------------------------------------------------------
int load_user_elf(pgtable_t pt, const void *img, unsigned long img_size,
                  unsigned long *entry_out, struct vma **vma_list_out,
                  uint64_t *brk_out) {
    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)img;

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

    struct vma *vlist = 0;
    uint64_t highest_end = 0;

    for (int i = 0; i < ehdr->e_phnum; i++) {
        const Elf64_Phdr *ph = &phdrs[i];
        if (ph->p_type != PT_LOAD) continue;
        if (ph->p_memsz == 0) continue;

        unsigned long perm = PTE_U;
        if (ph->p_flags & PF_R) perm |= PTE_R;
        if (ph->p_flags & PF_W) perm |= PTE_W;
        if (ph->p_flags & PF_X) perm |= PTE_X;

        unsigned long va_start = ph->p_vaddr & ~(PAGE_SIZE - 1);
        unsigned long va_end   = page_round_up(ph->p_vaddr + ph->p_memsz);

        for (unsigned long va = va_start; va < va_end; va += PAGE_SIZE) {
            void *kpage = page_alloc();
            if (!kpage) {
                printk("exec: OOM loading segment\n");
                vma_list_free(&vlist);
                return -ENOMEM;
            }

            unsigned long page_end        = va + PAGE_SIZE;
            unsigned long copy_va_start   = (ph->p_vaddr > va) ? ph->p_vaddr : va;
            unsigned long copy_va_end     =
                (ph->p_vaddr + ph->p_filesz < page_end)
                ? ph->p_vaddr + ph->p_filesz : page_end;

            if (copy_va_start < copy_va_end) {
                unsigned long page_off = copy_va_start - va;
                unsigned long file_off = ph->p_offset + (copy_va_start - ph->p_vaddr);
                memmove((char *)kpage + page_off,
                        (const char *)img + file_off,
                        copy_va_end - copy_va_start);
            }

            // POSIX: bytes in [filesz, memsz) must read as zero. page_alloc()
            // already zeroes the whole page, so the tail is implicitly clean,
            // but we zero again here so the invariant is local to this loader
            // rather than coupled to the page allocator's behaviour.
            unsigned long file_end_va = ph->p_vaddr + ph->p_filesz;
            if (file_end_va < page_end) {
                unsigned long zero_start = (file_end_va > va) ? file_end_va : va;
                memset((char *)kpage + (zero_start - va), 0, page_end - zero_start);
            }

            vmem_map(pt, va, virt_to_phys((unsigned long)kpage), PAGE_SIZE, perm);
        }

        struct vma *seg_vma = vma_alloc();
        if (!seg_vma) {
            vma_list_free(&vlist);
            return -ENOMEM;
        }
        seg_vma->start = va_start;
        seg_vma->end   = va_end;
        seg_vma->prot  = 0;
        if (ph->p_flags & PF_R) seg_vma->prot |= VMA_PROT_R;
        if (ph->p_flags & PF_W) seg_vma->prot |= VMA_PROT_W;
        if (ph->p_flags & PF_X) seg_vma->prot |= VMA_PROT_X;
        seg_vma->type = VMA_TYPE_ANON;
        vma_insert(&vlist, seg_vma);

        if (va_end > highest_end)
            highest_end = va_end;
    }

    *entry_out = ehdr->e_entry;
    *vma_list_out = vlist;
    *brk_out = page_round_up(highest_end);
    return 0;
}

// ---------------------------------------------------------------------------
// proc_spawn_setup_stdio — open /dev/console as fd 0/1/2 for a new process
// ---------------------------------------------------------------------------
static void proc_spawn_setup_stdio(struct pcb *p) {
    struct inode *con = devfs_console_inode();
    if (!con) return;

    for (int fd = 0; fd < 3; fd++) {
        struct file *f = filealloc();
        if (!f) return;
        f->type     = FD_INODE;
        f->readable = (fd == 0) ? 1 : 0;
        f->writable = (fd != 0) ? 1 : 0;
        f->off      = 0;
        f->ip       = inode_get(con);
        p->ofile[fd] = f;
    }
}

// ---------------------------------------------------------------------------
// proc_spawn — create a PROC_READY user process from a path in the VFS
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

    // Load the ELF.  Try VFS first (tarfs_init must have run); fall back to
    // the legacy tarfs_find for selftests that run before VFS is up.
    const void *img      = 0;
    unsigned long img_sz = 0;

    struct inode *ip = 0;
    if (namei(path, &ip) == 0 && ip) {
        // Read the entire file into a temporary bounce buffer.
        // tarfs inodes have a direct data pointer, so read at offset 0.
        img_sz = ip->size;
        // For tarfs: cast fs_data to access the raw data pointer directly,
        // avoiding a heap allocation (tarfs data is already in the kernel image).
        struct { const char *data; unsigned long file_size; } *d = ip->fs_data;
        img = d->data;
        inode_put(ip);
    }

    if (!img) {
        // Fallback: legacy flat tarfs lookup (e.g. during early boot).
        img = tarfs_find(path, &img_sz);
    }

    if (!img) {
        printk("proc_spawn: '%s' not found\n", path);
        free_proc(p);
        return 0;
    }

    struct vma *vlist = 0;
    uint64_t brk = 0;
    unsigned long entry;
    if (load_user_elf(p->pagetable, img, img_sz, &entry, &vlist, &brk) < 0) {
        free_proc(p);
        return 0;
    }
    p->vma_list = vlist;

    void *kstack = map_stack(p->pagetable);
    if (!kstack) {
        printk("proc_spawn: map_stack failed\n");
        free_proc(p);
        return 0;
    }

    // Heap VMA (zero-length initially)
    struct vma *heap_vma = vma_alloc();
    if (!heap_vma) {
        free_proc(p);
        return 0;
    }
    heap_vma->start = brk;
    heap_vma->end   = brk;
    heap_vma->prot  = VMA_PROT_R | VMA_PROT_W;
    heap_vma->type  = VMA_TYPE_HEAP;
    vma_insert(&vlist, heap_vma);

    // Stack VMA
    struct vma *stack_vma = vma_alloc();
    if (!stack_vma) {
        free_proc(p);
        return 0;
    }
    stack_vma->start = USER_STACK_TOP - PAGE_SIZE;
    stack_vma->end   = USER_STACK_TOP;
    stack_vma->prot  = VMA_PROT_R | VMA_PROT_W;
    stack_vma->type  = VMA_TYPE_STACK;
    vma_insert(&vlist, stack_vma);

    p->heap_vma   = heap_vma;
    p->brk_start  = brk;

    // Set up empty argv frame: [argc=0] [argv[0]=NULL] [envp[0]=NULL]
    uint64_t *frame = (uint64_t *)((char *)kstack + PAGE_SIZE - 24);
    frame[0] = 0;  // argc
    frame[1] = 0;  // argv[0] = NULL
    frame[2] = 0;  // envp[0] = NULL

    p->is_user    = 1;
    p->user_entry = entry;
    p->user_sp    = USER_STACK_TOP - 24;
    p->state      = PROC_READY;

    // Set up stdin/stdout/stderr → /dev/console.
    proc_spawn_setup_stdio(p);

    // Set cwd to VFS root.
    if (namei("/", &p->cwd) < 0) p->cwd = 0;
    p->cwd_path[0] = '/';
    p->cwd_path[1] = '\0';

    return p;
}
