#include<proc.h>

struct proc procs[NUMPROC];

struct proc *proczero;

int nextpid = 1;

void proc_init() {

    for(int i = 0; i < NUMPROC; i++) {
        void *phy_addr = page_alloc();
        unsigned long virt_addr = KSTACK(i);
        if(phy_addr == 0) {
            panic("Physical memory allocation failed while initiating kernel stacks!");
        }
        vmem_map(kernel_pgtable, virt_addr, (unsigned long)phy_addr, PAGE_SIZE, PTE_R | PTE_W);

        procs[i].pid = 0;
        procs[i].state = UNUSED;
        procs[i].killed = 0;
        procs[i].xstate = 0;
        procs[i].parent = 0;
        procs[i].kstack = virt_addr + PAGE_SIZE;
        procs[i].memsize = 0;
        procs[i].pgtable = 0;
    }

    proczero = &procs[0];

}

pgtable_t create_user_pgtable() {
    pgtable_t pgtable;
    pgtable = (pgtable_t) page_alloc();

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
        vmem_map(pgtable, vaddr+offset, (unsigned long)paddr, PAGE_SIZE, PTE_U | PTE_R | PTE_W | PTE_X);
    }

}

unsigned long map_stack(pgtable_t pgtable) {
    void *phyaddr = page_alloc();
    unsigned long vaddr = USER_STACK_BASE - PAGE_SIZE;

    if(phyaddr == 0) return 0;

    vmem_map(pgtable, vaddr, (unsigned long)phyaddr, PAGE_SIZE, PTE_U | PTE_R | PTE_W);
    return USER_STACK_BASE;
}
