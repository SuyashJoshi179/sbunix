#pragma once
#include <vmem.h>

struct pcb;

// Load an ELF binary image into the given user page table.
// Maps each PT_LOAD segment at its p_vaddr.
// Stores the ELF entry point in *entry_out.
// Returns 0 on success, -1 on failure.
int load_user_elf(pgtable_t pt, const void *img, unsigned long img_size,
                  unsigned long *entry_out);

// Allocate and fully initialize a PROC_READY user process from a tarfs path.
// Returns the PCB on success, NULL on failure.
struct pcb *proc_spawn(const char *path);
