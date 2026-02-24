.section .text.init
.global _start
_start:
	li sp, 0x88000000
	la t0, _bss_start
	la t1, _stack_top
	bge t0, t1, bss_done

clear_bss:
	sd zero, 0(t0)
	addi t0, t0, 8
	blt t0, t1, clear_bss

bss_done:
	call boot

end:
	j end
