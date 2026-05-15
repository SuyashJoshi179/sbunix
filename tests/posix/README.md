# POSIX Conformance Harness

Compile-only tests that pin our libc's declared prototypes to SUSv5 prototypes.
One C file per POSIX header. Run with `make posix-check`.

## How it works

Each `tests/posix/<header>.c` includes one POSIX header and contains:

1. A manifest comment listing audited and excluded functions.
2. Prototype "pins" — function-pointer assignments that force the compiler to
   verify our libc's declaration matches the SUSv5 prototype.
3. Optional struct-field references that fail to compile if a POSIX-required
   field is missing.

A test fails iff our header diverges from POSIX. Failures are expected output
in Phase 1; Phase 2 fix plans land remediations.

## Reference

POSIX prototypes are pulled from `docs/susv5-html/basedefs/<header>.h.html`.
The harness uses strict compile flags so any warning is a failure.

## Pin convention

Each pin is `__attribute__((unused)) static <RETURN> (*_pin_<name>)(<PARAMS>) = <name>;`
The macro `PIN` at the top of each file expands to the attribute prefix.

## Divergence-comment convention

When a pin's LHS uses a POSIX type that is not defined in our libc (e.g.
`clockid_t`, `timer_t`), the parser cascades and downstream pins in the same
file become unreachable. To keep the remaining pins testable, comment out
such pins with a `DIVERGENCE:` marker and a note describing what is missing:

```c
/* DIVERGENCE: clockid_t typedef missing from our libc; uncomment after
 * Phase 2 adds the typedef to <sys/types.h>.
 * PIN int (*_pin_clock_gettime)(clockid_t, struct timespec *) = clock_gettime;
 */
```

Every commented-out pin must be recorded in `docs/posix-audit-2026-05.md` as
its own divergence row. Phase 2 fix plans uncomment these pins after landing
the typedef/struct/macro the comment references.
