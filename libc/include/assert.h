/* Re-includable: assert is redefined per NDEBUG state on each #include. */
#undef assert

void __assert_fail(const char *expr, const char *file, int line,
                   const char *func) __attribute__((noreturn));

#ifdef NDEBUG
#define assert(e) ((void)0)
#else
#define assert(e) \
    ((e) ? (void)0 : __assert_fail(#e, __FILE__, __LINE__, __func__))
#endif

#ifndef static_assert
#define static_assert _Static_assert
#endif
