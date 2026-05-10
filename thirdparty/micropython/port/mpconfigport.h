// SBUnix port: minimal MicroPython config.
// Targets RV64 soft-float, in-tree libc, no FP, setjmp-based NLR.

#include <stdint.h>
#include <alloca.h>

// Minimum starting config — turn things on explicitly.
#define MICROPY_CONFIG_ROM_LEVEL            (MICROPY_CONFIG_ROM_LEVEL_MINIMUM)

// Compiler/runtime knobs.
#define MICROPY_ENABLE_COMPILER             (1)
#define MICROPY_ENABLE_GC                   (1)
#define MICROPY_HELPER_REPL                 (0)
#define MICROPY_ENABLE_EXTERNAL_IMPORT      (1)
#define MICROPY_MODULE_FROZEN_MPY           (0)
#define MICROPY_MODULE_FROZEN_STR           (0)
#define MICROPY_PERSISTENT_CODE_LOAD        (1)

// File reading via POSIX open/read/close (we have those in libc).
#define MICROPY_READER_POSIX                (1)

// No hardware FP, no math.
#define MICROPY_FLOAT_IMPL                  (MICROPY_FLOAT_IMPL_NONE)

// No RV64 native NLR; fall back to libc setjmp/longjmp.
#define MICROPY_NLR_SETJMP                  (1)

// gchelper has no rv64 register grabber; use the setjmp-based fallback.
#define MICROPY_GCREGS_SETJMP               (1)

// libc owns errno.
#define MICROPY_USE_INTERNAL_ERRNO          (0)

// Allocator headroom.
#define MICROPY_ALLOC_PATH_MAX              (256)
#define MICROPY_ALLOC_PARSE_CHUNK_INIT      (16)

// Static GC heap (256 KiB) — avoid early sbrk dependence.
#define MICROPY_GC_HEAP_SIZE                (256 * 1024)

// Optional features kept off for first-link.
#define MICROPY_PY_ASYNC_AWAIT              (0)
#define MICROPY_PY_BUILTINS_HELP            (0)
#define MICROPY_PY_SYS                      (1)
#define MICROPY_PY_SYS_EXIT                 (1)
#define MICROPY_PY_SYS_PLATFORM             "sbunix"
#define MICROPY_PY_IO                       (0)
#define MICROPY_PY_BUILTINS_FLOAT           (0)
#define MICROPY_PY_BUILTINS_COMPLEX         (0)
#define MICROPY_PY_MATH                     (0)
#define MICROPY_PY_CMATH                    (0)
#define MICROPY_PY_TIME                     (0)
#define MICROPY_PY_THREAD                   (0)

// Type definitions for an LP64 target (matches our riscv64 lp64 ABI).
typedef intptr_t  mp_int_t;
typedef uintptr_t mp_uint_t;
typedef long      mp_off_t;

#define MICROPY_HW_BOARD_NAME "sbunix"
#define MICROPY_HW_MCU_NAME   "rv64imac"

#define MP_STATE_PORT MP_STATE_VM
