#pragma once

/* RV64 callee-saves: ra, sp, s0..s11 = 14 longs.
 * Sized to 32 longs to leave room for fs0..fs11 (FP) without an ABI
 * break when soft-float is replaced by hard-float. */
typedef unsigned long jmp_buf[32];
typedef unsigned long sigjmp_buf[32];

int  setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val) __attribute__((noreturn));

int  sigsetjmp(sigjmp_buf env, int savemask);
void siglongjmp(sigjmp_buf env, int val) __attribute__((noreturn));

#define _setjmp(e)         setjmp(e)
#define _longjmp(e, v)     longjmp((e), (v))
