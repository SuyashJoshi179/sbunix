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
