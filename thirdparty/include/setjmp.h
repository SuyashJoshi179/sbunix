#pragma once

typedef unsigned long jmp_buf[14]; /* ra sp s0-s11 */

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val) __attribute__((noreturn));
#define sigsetjmp(env, save) setjmp(env)
#define siglongjmp(env, val) longjmp(env, val)
typedef jmp_buf sigjmp_buf;
