#include <elf.h>
#include <exec.h>
#include <file.h>
#include <inode.h>
#include <pmem.h>
#include <printk.h>
#include <proc.h>
#include <string.h>
#include <tarfs.h>
#include <vfs.h>
#include <vmem.h>

// Declared in devfs.c.
struct inode *devfs_console_inode(void);

// ---------------------------------------------------------------------------
// load_user_elf — map an ELF binary into a user page table
// ---------------------------------------------------------------------------
int load_user_elf(pgtable_t pt, const void *img, unsigned long img_size,
                  unsigned long *entry_out) {
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
                return -1;
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

            vmem_map(pt, va, virt_to_phys((unsigned long)kpage), PAGE_SIZE, perm);
        }
    }

    *entry_out = ehdr->e_entry;
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
        free_user_pgtable(p->pagetable);
        free_proc(p);
        return 0;
    }

    unsigned long entry;
    if (load_user_elf(p->pagetable, img, img_sz, &entry) < 0) {
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

    // Set up stdin/stdout/stderr → /dev/console.
    proc_spawn_setup_stdio(p);

    // Set cwd to VFS root.
    if (namei("/", &p->cwd) < 0) p->cwd = 0;
    p->cwd_path[0] = '/';
    p->cwd_path[1] = '\0';

    printk("proc_spawn: spawned pid=%d from '%s', entry=0x%lx\n",
           p->pid, path, entry);
    return p;
}
