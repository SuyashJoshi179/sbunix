#pragma once
#include <stdint.h>
#include <vmem.h>

/* ELF64 header */
typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

/* ELF64 program header */
typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

#define ET_EXEC   2
#define PT_LOAD   1
#define PF_X      1
#define PF_W      2
#define PF_R      4
#define ELF_MAGIC 0x464C457FU   /* "\x7fELF" little-endian */

/* Load ELF segments into pgtable.
   Returns entry point, or 0 on failure. */
uint64_t elf_load(pgtable_t pgtable, char *img, unsigned long size);
