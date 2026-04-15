#pragma once

void plic_init(void);

/* Claim the highest-priority pending interrupt for S-mode context.
 * Returns the IRQ number (> 0) or 0 if none pending. */
int  plic_claim(void);

/* Signal completion of the given IRQ to the PLIC. */
void plic_complete(int irq);
