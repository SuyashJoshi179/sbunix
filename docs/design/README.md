# SBUnix Design Docs

Per-phase low-level design documents for the SBUnix completion plan in
`docs/SBUnix_Roadmap.md`. Each doc expands one phase (or sub-phase
group) into file-level detail: data structures, key flows, syscall ABI,
test plan, and gotchas specific enough that implementation can start
cold.

These are written **ahead of implementation** to catch cross-cutting
decisions before they turn into churn. When a phase lands, the "Exit
criteria" section of its doc doubles as the PR's acceptance checklist.

## Index

| Phase | Doc | Status |
|---|---|---|
| 3 tail | [phase3_tail.md](phase3_tail.md) | design ready, not implemented |
| 4 | [phase4_fds_vfs.md](phase4_fds_vfs.md) | design ready |
| 5 | [phase5_virtio_sbfs.md](phase5_virtio_sbfs.md) | design ready |
| 6 | [phase6_shell.md](phase6_shell.md) | design ready |
| 7 | [phase7_memory.md](phase7_memory.md) | design ready |
| 7.5 | [phase7_5_micropython.md](phase7_5_micropython.md) | design ready |
| 8 | [phase8_signals.md](phase8_signals.md) | design ready |
| 9 | [phase9_cleanup.md](phase9_cleanup.md) | design ready |
| 10 | [phase10_busybox.md](phase10_busybox.md) | aspirational, open-ended |

## How to read these

Each doc is self-contained but assumes the reader has read
`docs/SBUnix_Roadmap.md` once. If a design decision here conflicts with
the roadmap, the roadmap wins — file an update to both.

Sections are consistent across docs:

1. **Goal** — one paragraph of what's being built and why.
2. **Preconditions** — what must already exist from prior phases.
3. **Concepts & data structures** — the nouns of the design.
4. **File-by-file changes** — new files, modified files, and what
   lives in each.
5. **Key flows** — walk-throughs of the tricky control paths.
6. **Syscall ABI & error codes**.
7. **Test plan** — kernel selftests + user-space tests.
8. **Gotchas** — phase-specific traps, complementing the roadmap.
9. **Open questions** — things deliberately deferred for this PR.
10. **Exit criteria** — the checklist to call the phase done.
