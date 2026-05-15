#pragma once
#include <stdint.h>
#include <vmem.h>
#include <file.h>    // struct file, NOFILE
#include <inode.h>   // struct inode
#include <vma.h>
#include <resource.h> // struct rlimit, RLIMITS_NR
#include <signal.h>  // sigset_t, struct sigaction, NSIG

#define KSTACK_SIZE PAGE_SIZE   // one 4KB page per process kernel stack

// Callee-saved register set used by swtch() for kernel-to-kernel switches.
struct context {
    uint64_t ra;    // offset 0
    uint64_t sp;    // offset 8
    uint64_t s0;    // offset 16
    uint64_t s1;    // offset 24
    uint64_t s2;    // offset 32
    uint64_t s3;    // offset 40
    uint64_t s4;    // offset 48
    uint64_t s5;    // offset 56
    uint64_t s6;    // offset 64
    uint64_t s7;    // offset 72
    uint64_t s8;    // offset 80
    uint64_t s9;    // offset 88
    uint64_t s10;   // offset 96
    uint64_t s11;   // offset 104
};

typedef enum {
    PROC_UNUSED   = 0,
    PROC_READY    = 1,
    PROC_RUNNING  = 2,
    PROC_SLEEPING = 3,
    PROC_ZOMBIE   = 4,
    PROC_STOPPED  = 5,
} proc_state_t;

struct pcb {
    int            pid;
    uint64_t       generation;      // bumped on each alloc_proc; pid-recycle guard
    int            parent_pid;
    int            exit_status;
    proc_state_t   state;

    // Job control: process group, session, last-stop signal, wait4 latches.
    int            pgid;
    int            sid;
    int            last_signal;            // last signal that stopped/terminated
    uint8_t        stopped_reported;       // wait4(WUNTRACED) already reported
    uint8_t        continued_pending;      // SIGCONT seen, wait4(WCONTINUED) pending
    uint8_t        did_exec;               // child has called execve at least once

    uint8_t        is_user;         // 1 for user processes, 0 for kernel threads

    // Executable name (basename of last exec'd path or argv[0]); NUL-padded.
    // Surfaced via the Name: field in /proc/<pid>/status and the comm in
    // /proc/<pid>/stat — there is no separate /proc/<pid>/comm file.
    char           comm[16];

    void         (*entry)(void);    // entry function (kernel threads only)

    // User process fields (populated by exec/spawn; 0 for kernel threads)
    pgtable_t      pagetable;       // user page table root (kernel virtual addr)
    unsigned long  user_entry;      // ELF entry point
    unsigned long  user_sp;         // user stack pointer

    struct context context;         // saved registers for swtch()
    void          *kstack_page;     // kernel stack page (kernel virtual addr)

    // Timer-based sleep: tick count at which this proc should wake.
    // 0 means "not sleeping on a deadline" (sleeping on an event instead).
    uint64_t       wake_tick;

    // Tick count at which to deliver SIGALRM (0 = no alarm pending).
    uint64_t       alarm_tick;

    // Channel-based sleep: non-NULL while sleeping on a specific address.
    void          *sleep_chan;

    // File descriptor table (Phase 4).
    struct file   *ofile[NOFILE];   // open files; null = free slot
    struct inode  *cwd;             // current working directory (refcounted)
    char           cwd_path[256];   // string form of cwd, kept in sync by chdir

    // Path of the last exec'd image (or kernel-spawn path), copied verbatim
    // from the caller-supplied string — may be relative. NUL-terminated;
    // empty for kernel threads. Used by /proc/<pid>/exe.
    char           exe_path[256];

    // Virtual memory areas (Phase 7)
    struct vma    *vma_list;        // sorted VMA list head
    struct vma    *heap_vma;        // pointer to heap VMA for fast sbrk
    uint64_t       brk_start;       // ELF data end, page-aligned up

    // Signals (Phase 8)
    sigset_t       sig_pending;     // bitmap of pending signals
    sigset_t       sig_blocked;     // bitmap of blocked signals (never has SIGKILL/SIGSTOP)
    sigset_t       sig_saved_mask;  // mask saved by signal delivery, restored by sigreturn
    sigset_t       sig_suspend_saved_mask;  // mask to restore when sigsuspend returns (POSIX)
    uint8_t        sig_suspend_active;      // 1 while inside sigsuspend → restore saved_mask
    struct sigaction sig_handlers[NSIG];
    uint8_t        in_sighandler;   // 1 while a user signal handler is running
    uint8_t        delivering_segv; // guard against recursive SIGSEGV default-kill

    // POSIX sigaltstack. ss_flags starts at SS_DISABLE. "Currently running
    // on alt" is derived on-demand from the saved user SP (see
    // sp_on_altstack in signal.c) — no sticky flag, so a handler that
    // exits via longjmp doesn't leave the kernel mis-tracking state.
    // Inherited across fork, reset to disabled on exec.
    stack_t        sig_altstack;

    // POSIX rlimit table (indexed by resource id; sparse).
    struct rlimit  rlim[RLIMITS_NR];

    // CPU time accounting in HZ ticks (HZ = TICKS_PER_SEC = CLOCKS_PER_SEC).
    // Incremented by timer_handler against current_proc based on SPP at trap
    // time. cu/cstime accumulate reaped children's totals (POSIX times()).
    uint64_t       utime_ticks;
    uint64_t       stime_ticks;
    uint64_t       cutime_ticks;
    uint64_t       cstime_ticks;
    struct pcb    *next;            // intrusive linked list
};

void  sched_init(void);
void  yield(void);
void  swtch(struct context *old, struct context *new);
void  proc_stop_current(void);

struct pcb *current_proc(void);
struct pcb *proc_list_head(void);  // for timer_handler sleeper scan
struct pcb *proc_find_by_pid(int pid);
struct pcb *alloc_proc(void);
void        free_proc(struct pcb *p);

// Set p->comm to the basename of `src` (truncated, NUL-terminated). Used by
// proc_spawn (init image) and do_exec (argv[0] basename) so the Name: field
// in /proc/<pid>/status and the comm in /proc/<pid>/stat (and ps) reflect
// the running program.
void        proc_set_comm_basename(struct pcb *p, const char *src);

void proc_exit_current(int status);
int  proc_wait_current(int *status);
int  proc_wait4_current(int pid, int *status, int options);
int  proc_fork_current(void);
void proc_sleep(struct pcb *p);
void proc_sleep_ms(uint64_t ms);   // timed sleep
void proc_wakeup(int pid);
void proc_sleep_chan(void *chan);
void proc_wakeup_chan(void *chan);
