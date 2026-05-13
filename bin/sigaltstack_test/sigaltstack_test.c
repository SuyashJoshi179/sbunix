#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>

/* sigaltstack(2) — API + actual SP switch.
 *
 * Closes the largest OPTS LINK cluster (~105 TUs). Covers:
 *   - default state (SS_DISABLE)
 *   - set + read-back
 *   - SS_DISABLE
 *   - size < MINSIGSTKSZ -> ENOMEM
 *   - unknown flag bit  -> EINVAL
 *   - SA_ONSTACK actually delivers a handler with sp inside the alt
 *     stack range
 */

static int fails = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("sigaltstack_test: FAIL %s\n", msg); fails++; } \
} while (0)

static char alt_buf[SIGSTKSZ];
static volatile int handler_saw_alt_sp = 0;
static volatile uintptr_t handler_sp = 0;

static void usr1_handler(int sig) {
    (void)sig;
    /* sp at the moment the handler runs: read from a local. */
    int x;
    handler_sp = (uintptr_t)&x;
    uintptr_t lo = (uintptr_t)alt_buf;
    uintptr_t hi = lo + sizeof(alt_buf);
    handler_saw_alt_sp = (handler_sp >= lo && handler_sp < hi);
}

int main(void) {
    stack_t ss, oss;

    /* Default: disabled. */
    memset(&oss, 0xAA, sizeof(oss));
    CHECK(sigaltstack(NULL, &oss) == 0,        "read default ok");
    CHECK(oss.ss_flags & SS_DISABLE,           "default ss_flags has SS_DISABLE");

    /* size below floor -> ENOMEM (with SS_DISABLE clear). */
    ss.ss_sp = alt_buf; ss.ss_size = MINSIGSTKSZ - 1; ss.ss_flags = 0;
    CHECK(sigaltstack(&ss, NULL) == -1 && errno == ENOMEM, "small size ENOMEM");

    /* Unknown flag -> EINVAL. */
    ss.ss_sp = alt_buf; ss.ss_size = SIGSTKSZ; ss.ss_flags = 0x100;
    CHECK(sigaltstack(&ss, NULL) == -1 && errno == EINVAL, "unknown flag EINVAL");

    /* Install valid altstack; oss reflects previous (disabled). */
    ss.ss_sp = alt_buf; ss.ss_size = SIGSTKSZ; ss.ss_flags = 0;
    memset(&oss, 0xAA, sizeof(oss));
    CHECK(sigaltstack(&ss, &oss) == 0,         "install altstack ok");
    CHECK(oss.ss_flags & SS_DISABLE,           "previous was disabled");

    /* Read back. */
    memset(&oss, 0xAA, sizeof(oss));
    CHECK(sigaltstack(NULL, &oss) == 0,        "read after install");
    CHECK(oss.ss_sp == alt_buf,                "ss_sp round-trip");
    CHECK(oss.ss_size == (size_t)SIGSTKSZ,     "ss_size round-trip");
    CHECK((oss.ss_flags & SS_DISABLE) == 0,    "not disabled after install");
    CHECK((oss.ss_flags & SS_ONSTACK) == 0,    "not on stack while idle");

    /* Wire SIGUSR1 with SA_ONSTACK and raise. Handler must observe its
     * SP inside alt_buf. */
    struct sigaction sa = {0};
    sa.sa_handler = usr1_handler;
    sa.sa_flags   = SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    CHECK(sigaction(SIGUSR1, &sa, NULL) == 0,  "sigaction SA_ONSTACK ok");
    handler_saw_alt_sp = 0; handler_sp = 0;
    raise(SIGUSR1);
    CHECK(handler_saw_alt_sp == 1,             "handler sp inside altstack");

    /* After handler returns, SS_ONSTACK must be clear. */
    memset(&oss, 0xAA, sizeof(oss));
    CHECK(sigaltstack(NULL, &oss) == 0,        "read after delivery");
    CHECK((oss.ss_flags & SS_ONSTACK) == 0,    "SS_ONSTACK clear post-handler");

    /* Without SA_ONSTACK, handler must run on normal stack. */
    sa.sa_flags = 0;
    CHECK(sigaction(SIGUSR1, &sa, NULL) == 0,  "sigaction without SA_ONSTACK");
    handler_saw_alt_sp = 0; handler_sp = 0;
    raise(SIGUSR1);
    CHECK(handler_saw_alt_sp == 0,             "handler sp NOT in altstack");

    /* SS_DISABLE clears the altstack. */
    ss.ss_sp = NULL; ss.ss_size = 0; ss.ss_flags = SS_DISABLE;
    CHECK(sigaltstack(&ss, NULL) == 0,         "SS_DISABLE accepted");
    memset(&oss, 0xAA, sizeof(oss));
    CHECK(sigaltstack(NULL, &oss) == 0,        "read after disable");
    CHECK(oss.ss_flags & SS_DISABLE,           "altstack disabled");

    if (fails == 0) {
        printf("sigaltstack_test: PASS\n");
        return 0;
    }
    printf("sigaltstack_test: %d FAIL\n", fails);
    return 1;
}
