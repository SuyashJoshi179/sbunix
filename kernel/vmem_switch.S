.section .text
.global vmem_switch_to_high
vmem_switch_to_high:
	csrw satp, a0
	sfence.vma zero, zero
	add sp, sp, a1
	add ra, ra, a1
	ret
