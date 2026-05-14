/* Weak fallbacks for sigemptyset/sigfillset/sigaddset.
 *
 * Kept in a separate translation unit that intentionally does NOT include
 * <signal.h>: a libc whose signal.h provides these as `static inline` would
 * otherwise cause a "redefinition" error when compat.c also tries to define
 * them. Static inline has internal linkage, so no symbol conflict at link
 * time -- callers using the inline body get it; libcs without these get our
 * weak symbol.
 *
 * Assumes sigset_t is byte-compatible with `unsigned long` (8 bytes), which
 * matches every SBUnix libc seen to date. C linkage is by name only, not
 * parameter type, so this resolves correctly against callers that pass a
 * differently-typed sigset_t pointer. */

typedef unsigned long _sbu_sigset;

__attribute__((weak)) int sigemptyset(_sbu_sigset *set) {
	*set = 0;
	return 0;
}
__attribute__((weak)) int sigfillset(_sbu_sigset *set) {
	*set = ~(_sbu_sigset)0;
	return 0;
}
__attribute__((weak)) int sigaddset(_sbu_sigset *set, int sig) {
	*set |= (_sbu_sigset)1 << sig;
	return 0;
}
