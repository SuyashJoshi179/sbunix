# Minimal user-mode test program embedded in the kernel image.
# Copied to a freshly-allocated page and mapped at USER_TEXT_BASE (0x1000)
# in the test user process's address space.
#
# Uses PC-relative addressing (auipc + addi) so byte-for-byte copies work at
# any virtual address.  .option norvc forces all 4-byte instructions so the
# fixed offset from auipc to the string is easy to count:
#   10 instructions * 4 bytes = 40 bytes → string at auipc_addr + 40
#
# Program: SYS_write(1, "user ok!\n", 9) then SYS_exit(0)

.section .text
.option norvc
.global user_test_start
.global user_test_end

user_test_start:
    auipc a1, 0         # a1 = PC (= virtual address of this instruction)
    addi  a1, a1, 40    # a1 = address of user_test_str (40 bytes below)
    li    a2, 9         # len = 9 ("user ok!\n")
    li    a0, 1         # fd = 1 (stdout)
    li    a7, 2         # SYS_write
    ecall
    li    a7, 1         # SYS_exit
    li    a0, 0         # exit status = 0
    ecall
1:  j 1b               # should not reach here; spin

# String is exactly at offset 40 from user_test_start (10 * 4-byte instructions)
user_test_str:
    .ascii "user ok!\n"
user_test_end:
