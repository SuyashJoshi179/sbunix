#include <signal.h>
#include <unistd.h>

extern void __sigtramp(void);

static long ecall0(long num) {
    register long _a7 asm("a7") = num;
    register long _a0 asm("a0") = 0;
    asm volatile("ecall" : "+r"(_a0) : "r"(_a7) : "memory");
    return _a0;
}

static long ecall2(long num, long a0, long a1) {
    register long _a7 asm("a7") = num;
    register long _a0 asm("a0") = a0;
    register long _a1 asm("a1") = a1;
    asm volatile("ecall" : "+r"(_a0) : "r"(_a7), "r"(_a1) : "memory");
    return _a0;
}

static long ecall3(long num, long a0, long a1, long a2) {
    register long _a7 asm("a7") = num;
    register long _a0 asm("a0") = a0;
    register long _a1 asm("a1") = a1;
    register long _a2 asm("a2") = a2;
    asm volatile("ecall" : "+r"(_a0) : "r"(_a7), "r"(_a1), "r"(_a2) : "memory");
    return _a0;
}

int kill(int pid, int sig) {
    return (int)ecall2(90, (long)pid, (long)sig);
}

int sigaction(int sig, const struct sigaction *act, struct sigaction *oldact) {
    struct sigaction kact;
    const struct sigaction *actp = act;

    if (act) {
        kact = *act;
        kact.sa_restorer = __sigtramp;
        actp = &kact;
    }

    return (int)ecall3(91, (long)sig, (long)actp, (long)oldact);
}

sighandler_t signal(int sig, sighandler_t handler) {
    struct sigaction sa;
    struct sigaction old;

    sa.sa_handler = handler;
    sa.sa_mask = 0;
    sa.sa_flags = 0;
    sa.sa_restorer = __sigtramp;

    if (sigaction(sig, &sa, &old) < 0)
        return SIG_IGN;
    return old.sa_handler;
}

int sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
    return (int)ecall3(92, (long)how, (long)set, (long)oldset);
}

int raise(int sig) {
    return kill(getpid(), sig);
}

int pause(void) {
    return (int)ecall0(94);
}
