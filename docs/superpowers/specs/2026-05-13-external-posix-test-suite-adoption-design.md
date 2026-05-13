# External POSIX test suites for grader-coverage discovery

**Status:** design, 2026-05-13
**Goal:** vendor Open POSIX Test Suite (OPTS) and the Sortix regress suite locally as dev-only gap-discovery tools. Use their compile and runtime failures to drive in-tree libc/kernel fixes that close the gaps the grader probes.
**Non-goals:** making OPTS or Sortix pass end-to-end (pthread/AIO/mq tests will never compile — we don't have those subsystems). Shipping the suites in `make submit`. Integrating into the init test list. Becoming a regression net in CI.

## Workflow in one paragraph

`make fetch` pulls a pinned upstream tarball into a gitignored path. `make opts` (or `make sortix`) tries to compile and link every test against our libc, writes a single results file. Engineer reads results, prioritises clusters that look grader-relevant (cross-referenced against `evalmessages_compiled.txt` and `submission_audit_findings.md`), fixes the libc/kernel on a feature branch, re-runs. For tests that link, a separate VirtIO disk image carries the binaries into the running kernel; `opts_run` walks the mount and tallies PTS_* outcomes.

## Layout

```
thirdparty/
  open-posix/
    upstream/                 # gitignored
    build/                    # gitignored
    Makefile                  # committed (~80 LoC)
    sanity/known_gap.c        # committed; smoke test for the gate itself
  sortix-regress/             # phase 2; same shape
bin/
  opts_run/opts_run.c         # committed; in-tree but NOT in init.c test list
```

`thirdparty/Makefile`'s dispatcher already skips subdirs without an `install` target. We give neither suite an `install` target, so they're invisible to `make thirdparty` and to `make submit`.

`thirdparty/.gitignore` gains `open-posix/upstream/`, `open-posix/build/`, `sortix-regress/upstream/`, `sortix-regress/build/`.

## Per-suite Makefile

Mirrors the root Makefile's CFLAGS exactly (rv64imac soft-float, in-tree libc) with two changes: drop `-Werror` (OPTS warnings shouldn't terminate the scan), add `-I$(UPSTREAM)/include` (for `posixtest.h`).

Targets:

| Target | What it does |
|---|---|
| `make fetch` | curl pinned tarball, verify sha256, unpack into `upstream/`. Idempotent. |
| `make opts` | walk all `.c` under `upstream/` (conformance + functional + stress, tag origin in output). Per TU: try to compile (`-c`), then link if compile succeeds. Record one of `BUILD-OK` / `BUILD-FAIL` / `LINK-FAIL` plus first error line into `build/results.txt`. Never bails on error. |
| `make gap-list` | run the cluster script over `results.txt`, write `build/gap-list.md`. |
| `make opts-disk` | take BUILD-OK binaries, pack into `build/opts-disk.img` via `tools/mkfs`. |
| `make clean` | rm `build/`. |

A small `thirdparty/open-posix/scripts/cluster-errors.sh` (~30 lines, sed/awk) reads `results.txt` and emits `gap-list.md` with two sections:
- **Fix** — clusters that look like real libc/kernel gaps (missing header that we should have, undeclared decl, wrong type, etc.), sorted by TU count.
- **Out-of-scope** — clusters we won't address (missing pthread, AIO, mqueue, realtime), with counts so we don't re-investigate.

The script's bucket-config (regex → label → in-scope?) lives in the script itself; editable as we learn.

## Delivery to running kernel

Separate VirtIO disk image (`build/opts-disk.img`), not tarfs. Reason: zero chance of `make submit` accidentally including the binaries. Run with `mount /dev/vdb /mnt/opts` from the shell, then `opts_run /mnt/opts`.

QEMU invocation needs a second `-drive` + `-device virtio-blk-device` pair behind a Makefile flag (e.g. `make qemu OPTS=1`); the kernel's existing `virtio_disk_init` claims the first device, so the second appears as `/dev/vdb`. The flag is dev-only — default `make qemu` is unchanged.

## opts_run

A ~100 LoC user binary at `bin/opts_run/`. Recursively walks the supplied directory tree (OPTS organises tests in nested per-function dirs), execs each regular file, captures exit code, maps to PTS_PASS/PTS_FAIL/PTS_UNRESOLVED/PTS_UNSUPPORTED/PTS_UNTESTED, prints per-test result + tally. Writes a machine-readable companion log next to the human one. Built by the standard `bin/*` wildcard rule; ends up at `/bin/opts_run` in the kernel. **Not added to `bin/init/init.c`'s test list** — we don't want it running at boot.

## Phase 2: Sortix regress

Identical shape: vendor under `thirdparty/sortix-regress/upstream/` (gitignored), `make fetch` + `make sortix` + `make sortix-disk`, reuse `opts_run` against a different mount (`/mnt/sortix`).

The exact upstream artifact is TBD when phase 2 starts. Most likely Sortix's `regress/` tree (small probe-style C, structurally similar to our existing `bin/*_test`). The team that originally inspired this work called it "OS-test via Sortix" — we'll inspect Sortix's source layout at the phase boundary to confirm whether they meant `regress/`, `tests/`, or a separate project.

Phase 2 starts only after phase 1 gap-list is triaged. Otherwise the two suites overlap and we won't know which one surfaced what.

## Integration validation

The build glue itself is code we need to trust. Two cheap checks:

1. `make fetch` verifies sha256 against a pinned constant; broken downloads fail loudly.
2. `thirdparty/open-posix/sanity/known_gap.c` is a ~15 LoC file that includes one missing header (`<aio.h>`) and one present header (`<unistd.h>`). The first should cluster into "Out-of-scope"; the second should compile clean. If `make opts` doesn't behave that way, the integration is broken.

## Decision log

| Decision | Choice | Why |
|---|---|---|
| Location | `thirdparty/<suite>/` (dispatcher-skipped) | Co-locates with port framework; dispatcher's "no install target = skip" already documented. |
| Vendoring | `make fetch` checksumed tarball | Reproducible, small repo, no submodule pain. |
| Scope | All upstream subdirs, tag origin in results | Don't preemptively drop functional/stress; tagging lets triage filter. |
| Triage | Two-section gap-list (Fix / Out-of-scope) | Out-of-scope counts useful as signal; don't drop them. |
| Delivery | Separate VirtIO disk | Strongest "not shipping in submit" guarantee. |
| Launcher | `bin/opts_run/` in-tree, NOT in init list | Reuses standard build path; explicit shell invocation prevents accidental boot-time runs. |
| Suite order | OPTS first, Sortix second | OPTS is the canonical POSIX surface; gap-list output drives the highest-leverage fixes first. |

## Out-of-scope behaviors (will not be addressed by closing OPTS gaps)

- pthreads / threading (`pthread.h`, `pthread_*`)
- POSIX AIO (`aio.h`)
- POSIX message queues (`mqueue.h`)
- POSIX realtime (`sched_*`, `timer_*`, `clock_nanosleep`)
- POSIX semaphores (`sem_*`)

OPTS tests that depend on these will cluster under Out-of-scope in `gap-list.md` and stay there.
