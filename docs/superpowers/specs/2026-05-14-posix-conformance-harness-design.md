# POSIX Conformance Harness — Phase 1 Design

**Status:** Approved 2026-05-14
**Phase:** 1 of N (harness + audit; fixes deferred to Phase 2+ plans)
**Branch context:** Builds on `feature/posix-complience`, which already shipped the
`gettimeofday` / `struct timezone` fix (PR #96) that motivated this work.

## Background

A grader review on a recent SBUnix submission flagged that `<sys/time.h>` declared
the time-of-day call with an opaque pointer second argument and never defined
`struct timezone`. External translation units that included our header could not
declare sibling routines using the POSIX-mandated tag, so the build failed before
any test binary was produced.

The single offending header has been corrected. The grader's note hinted that the
bug class is broader: any header we ship that omits a POSIX-required tag, typedef,
struct, or field can silently break consumers without producing a runtime
diagnostic on our side. A targeted patch is not enough — we need a mechanical
gate that catches this bug class for every function we expose, and we need it
before the next submission.

## Goal

Build a compile-only conformance harness that, for every POSIX function our libc
claims to implement, verifies a downstream user-space translation unit can
include the POSIX-mandated header and declare a sibling routine using the POSIX
types. Run the harness against the current libc and produce a divergence catalog
that becomes the input to Phase 2 fix plans.

## Non-goals (Phase 1)

- Fixing any divergences the harness uncovers (deferred to Phase 2).
- Link-time function existence checks (the grader's bug class is compile-time).
- `<limits.h>` value audits.
- Network/socket header audits.
- Wiring the gate into CI (deferred until Phase 2 turns the harness green).

## Architecture

Four components with clear boundaries:

### 1. Test files — `tests/posix/<header>.c`

One C file per POSIX header we ship. Each file:

- Includes only the header(s) POSIX assigns the function to under each function's
  SYNOPSIS in `docs/susv5-html/functions/<func>.html`. Including extra headers
  would mask cases where a required type is missing from the canonical one.
- Begins with a comment manifest listing the POSIX functions audited in this
  file and the non-POSIX names deliberately excluded (with a one-line reason).
- For each audited function, contains a *sibling declaration* — a standalone
  function prototype, written in the test file, whose return type and every
  parameter type use the POSIX-mandated typedefs and tags exactly as documented
  in `docs/susv5-html/functions/<func>.html`. The sibling is never defined or
  called; its purpose is to force the compiler to resolve every type name in the
  prototype against our headers, reproducing what an external test program does
  when it writes its own declarations.
- For each function whose signature references an aggregate type (`struct stat`,
  `struct timeval`, `struct passwd`, etc.) the file also references at least one
  field of that aggregate that POSIX requires, to surface missing-field bugs.

The file is self-contained: `gcc -c tests/posix/<header>.c -o /dev/null` is the
canonical test. No project state, no linkage step.

### 2. Build target — `make posix-check`

A single Makefile rule that iterates `tests/posix/*.c`, invokes the cross
compiler in strict mode per file, and reports per-file PASS/FAIL. Compile flags:

```
riscv64-unknown-elf-gcc -ffreestanding -nostdinc -isystem libc/include \
    -Werror -Wstrict-prototypes -Wmissing-prototypes -pedantic -std=c99 \
    -c -o /dev/null tests/posix/<header>.c
```

Rationale for each flag:

- `-ffreestanding -nostdinc -isystem libc/include` forces the toolchain to use
  *our* headers, not the host's. Without this the test would silently pass by
  picking up Linux headers.
- `-Werror` so any warning is a failure.
- `-Wstrict-prototypes -Wmissing-prototypes` catches K&R-style declarations the
  grader's stricter build would also reject.
- `-pedantic -std=c99` enforces ISO C, no GNU extensions, matching what an
  external test program is likely to be compiled under.

The rule exits non-zero if any file fails. Non-zero exit is informational in
Phase 1 (we expect divergence); it becomes a gate after Phase 2.

### 3. Coverage manifest (in-file)

Each test file's leading comment is the canonical declaration of "what POSIX
surface we claim to implement for this header". This file-local manifest is the
project's only place where the claim is recorded. Adding a new libc function
means editing the manifest in the relevant test file and adding the check; the
two cannot drift.

### 4. Audit doc — `docs/posix-audit-2026-05.md`

A pure output artifact, written by reading `make posix-check` stderr and
cross-referencing `docs/susv5-html/functions/<func>.html` for each failure. No
tooling generates it. Structure: one section per header. Per section:

- Functions audited (mirrors the in-file manifest).
- Divergences table: function | current proto in our header | POSIX proto |
  classification (missing struct / wrong return type / missing param / wrong
  header / missing field) | severity (build-breaker / latent / cosmetic) | fix
  sketch.
- Headers with zero divergences get a one-line "clean" entry.

This document is the deliverable that Phase 2 plans consume.

## Data flow

1. Author `tests/posix/<header>.c` files by walking `libc/include/<header>.h`
   against `docs/susv5-html/functions/<func>.html`.
2. Run `make posix-check`. Capture stderr.
3. For each compile failure, write an audit-doc entry.
4. Phase 1 complete when every header has an audit-doc section (clean or with
   divergences) and the harness reproduces the same red/green report on a fresh
   clone.

## Error handling

- Test compile failures are the expected initial state, not a build break.
  `make posix-check` is a diagnostic tool in Phase 1.
- `make` itself (default target) must remain green — `posix-check` is a
  side-target, never a dependency of `all`.
- Phase 2 plans land fixes; when `make posix-check` returns 0, that's Phase 2
  done.

## Testing the harness itself

The harness's own correctness has two failure modes: false-pass (a test compiles
when it shouldn't) and false-fail (a test fails for reasons unrelated to header
divergence).

Mitigation:

- Include at least one test for a function we already fixed (`gettimeofday` in
  `tests/posix/time.c`) so a PASS is provably achievable end-to-end.
- During harness authoring, deliberately introduce one breakage into one test
  file (e.g. reference a struct field that POSIX does not require), confirm
  FAIL diagnostic is informative, then remove the breakage before committing.
  This is a developer-discipline check, not a permanent artifact.

## Scope boundary recap

Phase 1 ships:

- `tests/posix/*.c` (one file per audited header)
- `make posix-check` target
- `docs/posix-audit-2026-05.md` (filled in)

Phase 1 does *not* ship:

- Any fix to a libc header to make a failing test pass.
- Any change to existing kernel code, existing libc implementation files, or
  existing userspace programs. The `tests/posix/` directory and the new
  Makefile target are additive only.
- CI integration.

## Open questions for the planning step

- Exact header list to audit in Phase 1 (some headers like network ones may be
  punted to a later phase entirely). The implementation plan resolves this by
  enumerating `libc/include/*.h` and classifying each as in-scope or deferred.
- Whether the audit-doc divergence table should be sortable by severity. Cosmetic;
  defer to authoring time.
