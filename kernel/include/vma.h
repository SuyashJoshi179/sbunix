#pragma once
#include <stdint.h>

struct inode;

#define VMA_PROT_R   0x1
#define VMA_PROT_W   0x2
#define VMA_PROT_X   0x4

#define VMA_TYPE_ANON  1
#define VMA_TYPE_FILE  2
#define VMA_TYPE_STACK 3
#define VMA_TYPE_HEAP  4

#define VMA_FLAG_COW    0x1
#define VMA_FLAG_SHARED 0x2

#define MAX_STACK_PAGES 256
#define MMAP_START      0x20000000UL
#define MMAP_END        (USER_STACK_TOP - (MAX_STACK_PAGES * 4096UL))
#define HEAP_MAX        MMAP_START

struct vma {
    uint64_t      start;
    uint64_t      end;
    uint32_t      prot;
    uint32_t      type;
    uint32_t      flags;
    struct inode *file;
    uint64_t      file_off;
    struct vma   *next;
};

struct vma *vma_alloc(void);
void        vma_free(struct vma *v);
struct vma *vma_find(struct vma *list, uint64_t va);
void        vma_insert(struct vma **list, struct vma *v);
void        vma_remove(struct vma **list, struct vma *v);
void        vma_list_free(struct vma **list);
struct vma *vma_list_dup(struct vma *src);

int  vma_split(struct vma **list, struct vma *v, uint64_t start, uint64_t end);
int  user_page_fault(uint64_t scause, uint64_t stval, uint64_t *trapframe);
