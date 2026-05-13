# External POSIX test suites for grader-coverage discovery

**Status:** design, 2026-05-13
**Goal:** vendor Open POSIX Test Suite (OPTS) and the Sortix regress suite locally as dev-only gap-discovery tools. Use their compile and runtime failures to drive in-tree libc/kernel fixes that close the gaps the grader probes.
**Non-goals:** making OPTS or Sortix pass end-to-end (pthread/AIO/mq tests will never compile — we don't have those subsystems). Shipping the suites in `make submit`. Integrating into the init test list. Becoming a regression net in CI.

## Workflow in one paragraph

`make fetch` pulls a pinned upstream tarball into a gitignored path. `make opts` (or `make sortix`) tries to compile and link every test against our libc, writes a single results file. Engineer reads results, prioritises clusters that look grader-relevant (cross-referenced against `evalmessages_compiled.txt` and `submission_audit_findings.md`), fixes the libc/kernel on a feature branch, re-runs. For tests that link, an opt-in tarfs overlay (`OPTS=1 make`) injects the binaries into `/bin/optsbin/` in the kernel; `opts_run` walks that directory and tallies PTS_* outcomes.

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

`thirdparty/Makefile`'s dispatcher iterates subdirs that have a `Makefile`. Each suite ships a Makefile whose `install` target is a **no-op by default** and only copies binaries to `$(ROOTFS)/optsbin/` (or `sortixbin/`) when `OPTS=1` (resp. `SORTIX=1`) is set in the environment. So the dispatcher harmlessly visits each suite under a default `make thirdparty`; nothing lands in rootfs.

`thirdparty/.gitignore` gains `open-posix/upstream/`, `open-posix/build/`, `sortix-regress/upstream/`, `sortix-regress/build/`.

## Per-suite Makefile

Mirrors the root Makefile's CFLAGS exactly (rv64imac soft-float, in-tree libc) with two changes: drop `-Werror` (OPTS warnings shouldn't terminate the scan), add `-I$(UPSTREAM)/include` (for `posixtest.h`).

Targets:

| Target | What it does |
|---|---|
| `make fetch` | curl pinned tarball, verify sha256, unpack into `upstream/`. Idempotent. |
| `make opts` | walk all `.c` under `upstream/` (conformance + functional + stress, tag origin in output). Per TU: try to compile (`-c`), then link if compile succeeds. Record one of `BUILD-OK` / `BUILD-FAIL` / `LINK-FAIL` plus first error line into `build/results.txt`. Never bails on error. |
| `make gap-list` | run the cluster script over `results.txt`, write `build/gap-list.md`. |
| `make install` (only when `OPTS=1`) | copy BUILD-OK binaries into `$(ROOTDIR)/build/rootfs/bin/optsbin/`. Default (no env var) is a no-op so the dispatcher never sees the binaries. |
| `make clean` | rm `build/` and the `optsbin/` overlay in rootfs. |

A small `thirdparty/open-posix/scripts/cluster-errors.sh` (~30 lines, sed/awk) reads `results.txt` and emits `gap-list.md` with two sections:
- **Fix** — clusters that look like real libc/kernel gaps (missing header that we should have, undeclared decl, wrong type, etc.), sorted by TU count.
- **Out-of-scope** — clusters we won't address (missing pthread, AIO, mqueue, realtime), with counts so we don't re-investigate.

The script's bucket-config (regex → label → in-scope?) lives in the script itself; editable as we learn.

## Delivery to running kernel

Opt-in tarfs overlay. The default kernel build is unchanged: `make` produces a kernel.elf with no OPTS binaries. With the env var, `OPTS=1 make` runs the OPTS suite Makefile's `install` target as part of the build, which copies BUILD-OK binaries into `build/rootfs/bin/optsbin/`. The root Makefile's existing `build/tarfs.o` rule then rolls them into tarfs, so they appear at `/bin/optsbin/<test>` in the running kernel.

**Why this is submit-safe.** Two independent guards apply:

1. `make submit` already rsync-excludes `thirdparty/` and `build/` (root Makefile, around line 114). The vendored OPTS source under `thirdparty/open-posix/upstream/` and the build artifacts under `build/rootfs/bin/optsbin/` are both unreachable from the submission tarball.
2. Reaching `optsbin/` requires explicitly setting `OPTS=1`. A developer who runs `make submit` without that flag produces a tree with no optsbin/ rootfs entries even before the rsync excludes apply.

Run with `opts_run /bin/optsbin/` from the shell.

The earlier-considered separate-VirtIO-disk delivery was infeasible: the kernel's `virtio_disk_init` claims only the first virtio-blk device and there is no `/dev/vdb`. Adding multi-disk support is real kernel work (see `missing_features.md:98-100`) and out of scope for this spec.

## opts_run

A ~100 LoC user binary at `bin/opts_run/`. Recursively walks the supplied directory tree (OPTS organises tests in nested per-function dirs), execs each regular file, captures exit code, maps to PTS_PASS/PTS_FAIL/PTS_UNRESOLVED/PTS_UNSUPPORTED/PTS_UNTESTED, prints per-test result + tally. Built by the standard `bin/*` wildcard rule; ends up at `/bin/opts_run` in the kernel. **Not added to `bin/init/init.c`'s test list** — we don't want it running at boot.

opts_run lives in the always-built `bin/` tree (not behind `OPTS=1`) because it's tiny, doesn't bring in OPTS test code, and is useful to have available even when an empty `optsbin/` is mounted. The OPTS-bigness lives entirely under `thirdparty/open-posix/`.

## Phase 2: Sortix regress

Identical shape: vendor under `thirdparty/sortix-regress/upstream/` (gitignored), same Makefile targets, install BUILD-OK binaries into `build/rootfs/bin/sortixbin/` under an `OPTS=1` (or a new `SORTIX=1`) env flag, reuse `opts_run` against `/bin/sortixbin/`.

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
| Delivery | Opt-in tarfs overlay via `OPTS=1` | Kernel currently supports only one virtio-blk device; separate disk would need kernel multi-disk work. Tarfs overlay + existing `make submit` thirdparty/build excludes give equivalent isolation with no kernel changes. |
| Launcher | `bin/opts_run/` in-tree, NOT in init list | Reuses standard build path; explicit shell invocation prevents accidental boot-time runs. |
| Suite order | OPTS first, Sortix second | OPTS is the canonical POSIX surface; gap-list output drives the highest-leverage fixes first. |

## Out-of-scope behaviors (will not be addressed by closing OPTS gaps)

- pthreads / threading (`pthread.h`, `pthread_*`)
- POSIX AIO (`aio.h`)
- POSIX message queues (`mqueue.h`)
- POSIX realtime (`sched_*`, `timer_*`, `clock_nanosleep`)
- POSIX semaphores (`sem_*`)

OPTS tests that depend on these will cluster under Out-of-scope in `gap-list.md` and stay there.
