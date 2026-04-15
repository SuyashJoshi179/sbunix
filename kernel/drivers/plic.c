#include <drivers/plic.h>
#include <stdint.h>

/* QEMU virt PLIC base address */
#define PLIC_BASE  0x0c000000UL

/* UART0 IRQ on QEMU virt */
#define UART_IRQ      10
/* VirtIO MMIO slot 0 IRQ (virtio-mmio-bus.0 = 0x10001000) */
#define VIRTIO_IRQ     1

/* Per xv6-riscv convention for a single-hart machine (hart 0):
 *   M-mode = context 0,  S-mode = context 1.
 *
 * Register layout:
 *   Priority (1 word per source): base + 4*irq
 *   Enable (128 bytes per context): base + 0x2000 + ctx*0x80
 *   Threshold: base + 0x200000 + ctx*0x1000
 *   Claim/complete: threshold_addr + 4
 */
#define PLIC_PRIORITY(irq)   (*(volatile uint32_t *)(PLIC_BASE + 4*(irq)))
#define PLIC_ENABLE_S        (*(volatile uint32_t *)(PLIC_BASE + 0x2000 + 1*0x80))
#define PLIC_THRESHOLD_S     (*(volatile uint32_t *)(PLIC_BASE + 0x200000 + 1*0x1000))
#define PLIC_CLAIM_S         (*(volatile uint32_t *)(PLIC_BASE + 0x200000 + 1*0x1000 + 4))

extern unsigned long mem_offset;

/* All MMIO registers are in the kernel upper-half map. */
#define PLIC_PRIORITY_K(irq) \
    (*(volatile uint32_t *)((PLIC_BASE + mem_offset) + 4*(irq)))
#define PLIC_ENABLE_S_K \
    (*(volatile uint32_t *)((PLIC_BASE + mem_offset) + 0x2000 + 1*0x80))
#define PLIC_THRESHOLD_S_K \
    (*(volatile uint32_t *)((PLIC_BASE + mem_offset) + 0x200000 + 1*0x1000))
#define PLIC_CLAIM_S_K \
    (*(volatile uint32_t *)((PLIC_BASE + mem_offset) + 0x200000 + 1*0x1000 + 4))

void plic_init(void) {
    /* Set UART IRQ priority to 1. */
    PLIC_PRIORITY_K(UART_IRQ) = 1;
    /* Enable UART IRQ in S-mode enable register (bit 10 of word 0). */
    PLIC_ENABLE_S_K |= (1u << UART_IRQ);

    /* Set VirtIO IRQ 1 priority to 1. */
    PLIC_PRIORITY_K(VIRTIO_IRQ) = 1;
    /* Enable VirtIO IRQ in S-mode enable register (bit 1 of word 0). */
    PLIC_ENABLE_S_K |= (1u << VIRTIO_IRQ);

    /* Set S-mode priority threshold to 0 (accept any priority ≥ 1). */
    PLIC_THRESHOLD_S_K = 0;
}

int plic_claim(void) {
    return (int)PLIC_CLAIM_S_K;
}

void plic_complete(int irq) {
    PLIC_CLAIM_S_K = (uint32_t)irq;
}
