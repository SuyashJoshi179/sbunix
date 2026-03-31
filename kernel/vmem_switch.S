.section .text
.global vmem_switch_to_high
vmem_switch_to_high:
	csrw satp, a0
	sfence.vma zero, zero
	add gp, gp, a1
	add s0, s0, a1
	add sp, sp, a1
	add ra, ra, a1
	ret
