#pragma once
#include <stdint.h>
#include <vmem.h>

struct pcb;
struct vma;
struct inode;

int load_user_elf(pgtable_t pt, struct inode *ip,
                  unsigned long *entry_out, struct vma **vma_list_out,
                  uint64_t *brk_out);

// Allocate and fully initialize a PROC_READY user process from a tarfs path.
// Returns the PCB on success, NULL on failure.
struct pcb *proc_spawn(const char *path);
