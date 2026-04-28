/* RV64 setjmp / longjmp.
 *
 * Soft-float ABI on this build (-march=rv64imac, -mabi=lp64), so we
 * save only the integer callee-saves: ra, sp, s0..s11. jmp_buf is
 * sized to 32 longs in setjmp.h to leave headroom for fs0..fs11 if
 * hard-float lands later — slot indices below stay stable.
 *
 * Layout (8 bytes each):
 *   [0]  ra
 *   [1]  sp
 *   [2]  s0
 *   [3]  s1
 *   ...
 *   [13] s11
 */

    .text
    .balign 4
    .global setjmp
    .global _setjmp
    .global sigsetjmp
    .type   setjmp, @function
setjmp:
_setjmp:
sigsetjmp:
    sd  ra,   0(a0)
    sd  sp,   8(a0)
    sd  s0,  16(a0)
    sd  s1,  24(a0)
    sd  s2,  32(a0)
    sd  s3,  40(a0)
    sd  s4,  48(a0)
    sd  s5,  56(a0)
    sd  s6,  64(a0)
    sd  s7,  72(a0)
    sd  s8,  80(a0)
    sd  s9,  88(a0)
    sd  s10, 96(a0)
    sd  s11,104(a0)
    li  a0,   0
    ret
    .size setjmp, . - setjmp

    .balign 4
    .global longjmp
    .global _longjmp
    .global siglongjmp
    .type   longjmp, @function
longjmp:
_longjmp:
siglongjmp:
    ld  ra,   0(a0)
    ld  sp,   8(a0)
    ld  s0,  16(a0)
    ld  s1,  24(a0)
    ld  s2,  32(a0)
    ld  s3,  40(a0)
    ld  s4,  48(a0)
    ld  s5,  56(a0)
    ld  s6,  64(a0)
    ld  s7,  72(a0)
    ld  s8,  80(a0)
    ld  s9,  88(a0)
    ld  s10, 96(a0)
    ld  s11,104(a0)
    /* C semantics: longjmp(env, 0) must cause setjmp to return 1. */
    mv  a0, a1
    bnez a0, 1f
    li  a0, 1
1:  ret
    .size longjmp, . - longjmp
