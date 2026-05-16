# SBUnix Release Branch Process

Playbook for cutting `release/X.Y.Z` off `develop` for grader submission.
The grader wants the core OS only — strip tests, helpers, docs,
thirdparty scaffolding, and dev tooling. The Makefile must stay
bit-identical to master so the grader's `make submit` / `make
thirdparty` hooks work.

## Tasks (in order)

1. **Pre-flight.** On `develop` with a clean tree, pull latest, run a
   clean build, smoke-boot under qemu, and confirm shell prompt
   appears and selftest reports 0 failures. If develop is broken,
   stop and fix develop first.

2. **Stash untracked files.** Anything in the working tree but
   untracked (especially under `docs/`) will be destroyed by the
   strip. Ask the user to stash or copy out before proceeding — this
   is the only user prompt in the playbook. Past run lost a
   `docs/manual-testing.md` here.

3. **Branch.** `git checkout -b release/X.Y.Z develop` with the version
   string the user supplied.

4. **Classify every `bin/<name>/` as production or test.** Walk the
   list, decide each one yourself, only escalate to the user if a
   binary is genuinely ambiguous after reading its source.

   - **Keep:** `init`, `sh`, and any POSIX coreutil (`cat`, `ls`,
     `mkdir`, `rm`, `mv`, `cp`, `ln`, `touch`, `stat`, `wc`, `head`,
     `tail`, `echo`, `pwd`, `date`, `sleep`, `kill`, `mount`, `ps`,
     `true`, `false`, `test`, etc.). If a name *might* be a POSIX
     utility you don't recognise, open the source to confirm before
     keeping.
   - **Strip:** anything ending in `_test`, containing `test`,
     ending in `_helper`, named `usertests`/`opts_run`/
     `headers_compile_gate`, or recognisable as a kernel-feature
     probe (memory/VM/IPC/stress: anything mentioning `mmap`, `cow`,
     `sbrk`, `stack`, `lazy`, `huge`, `pagecache`, `pcache`, `leak`,
     `stress`, `storm`, `churn`, `reap`, `pattern`, `realloc`,
     `calloc`, `free`, `oom`, `arena`, `bss`, `zerofill`,
     `demand_walk`, etc. — unless it's also on the keep list above).
   - **Ambiguous:** read `bin/<name>/<name>.c`. Test code asserts and
     reports pass/fail; production code does one user-visible
     operation and returns 0/1/errno.

   Print the keep set and strip set before deleting so the user can
   eyeball it.

5. **Strip `bin/`.** Loop and `rm -rf` everything not in your keep
   array. Use `bash -c '…'` with a quoted array, not zsh — zsh does
   not word-split unquoted `$VAR` and will delete every directory.
   Recovery: `git checkout HEAD -- bin/`.

6. **Remove kernel selftest.** Delete `kernel/selftest.c` and
   `kernel/include/selftest.h`. Patch `kernel/kernel.c` to drop the
   `#include <selftest.h>` line and the `selftest_run();` call.
   Comment-only mentions of "selftest" elsewhere in the kernel are
   harmless; leave them. Grep to confirm no stray refs remain.

7. **Strip the test-runner line from `/etc/rc`.** Open
   `rootfs/etc/rc` and delete the `/bin/runtests` line. The remaining
   script must still mount procfs, sbfs, and tmpfs and then
   `exec /bin/sh`. `bin/init/init.c` itself no longer needs editing —
   the boot-time test loop now lives in `/bin/runtests`, which step 5
   already strips (`runtests` matches the `contains test` rule).

8. **Remove dev artifacts.** `rm -rf docs thirdparty scripts tests`.
   Devicetree files (`my_devicetree.dtb`, `readable_devicetree.dts`)
   may be kept — they are reference material, not dev-only. Do
   **not** use `scripts/prep-submit.sh` — the user has disavowed it
   as wrong and unmaintained.

9. **Makefile bit-identical to master.** `git checkout master --
   Makefile`. `git diff master -- Makefile` must be empty. Leave the
   `thirdparty` target rule alone — the grader injects their own
   `thirdparty/` subtree and the rule is their compile hook.

   The reset also strips any develop-only targets (e.g.
   `posix-check`). This is safe iff step 8 has already removed the
   matching source tree (`tests/` for `posix-check`). Invariant:
   **every develop-only target must have its source tree deleted in
   step 8.** If you add a new dev-only target later, update step 8
   to drop its sources too.

10. **Build and smoke-test.** `make clean && make` must succeed. Then
    build the disk image and boot under qemu with a 20s timeout. The
    boot log must reach `sh>`, show all three userspace mounts
    (`procfs: mounted /proc`, `sbfs: mounted /mnt`, `tmpfs: mounted
    /tmp`), and contain **no `[SELFTEST]` lines**. If selftest still
    runs, step 6 was incomplete.

11. **Size-ceiling audit.** The `make submit` rsync has a silent
    `--max-size=100K` filter — any tracked file above 100KB is
    dropped from the submission without warning, which can produce a
    broken submission the grader receives without any local
    indication. Walk the post-strip tree and find every tracked file
    over 90KB (a 10KB safety margin below the rsync ceiling):

    ```bash
    find . -type f -not -path './.git/*' -not -path './build/*' \
                   -not -path './zig-out/*' -not -path './.zig-cache/*' \
                   -size +90k -printf '%s\t%p\n' | sort -rn
    ```

    If any file is listed, **halt the playbook and tell the user.**
    Print each oversized path with its size and explain:

    > "File `<path>` is `<size>` KB. The submit rsync drops anything
    > over 100KB silently. You need to either split this file before
    > submission or accept that it will not ship to the grader.
    > Cannot continue automatically."

    Do not proceed to commit. Resume only after the user has resolved
    each oversized file (usually by splitting the source). At the
    time of writing, the largest source is `kernel/syscall.c` at
    ~74KB — under the 90KB threshold, so the audit currently passes
    with no work needed.

12. **Commit.** Single commit on the release branch, descriptive
    message listing what was removed (counts) and what was kept
    (alphabetised names from your classifier). No co-author trailer
    (standing user preference). Do not use `--amend`.

13. **Push.** `git push -u origin release/X.Y.Z`. Release branches do
    not get PRs — they are submission snapshots. After a later rebase
    onto an updated develop, push with `--force-with-lease`.

14. **Submission goes via `make submit` on the prof's VM**, not
    locally. `$(SUBMIT_DIR)` defaults to `/submit`, which exists only
    on prof's infrastructure.

## Rebasing after develop advances

1. `git checkout develop && git pull`
2. `git checkout release/X.Y.Z && git rebase develop`
3. The strip commit is the only thing on the release branch, so
   conflicts are rare (strip touches files develop typically doesn't:
   deleted test bins, deleted selftest, rewritten init, patched
   kernel.c, master Makefile). Resolve any that arise.
4. Re-run steps 10 (build + smoke) and 12 (force-push with lease).

## Why these things, briefly

- **Selftest removal** also drops a hidden hack: pre-attaching procfs
  and sbfs before `/etc/rc`. With selftest gone, userspace `mount`
  becomes the genuine first attach — the real-OS behavior the grader
  expects.
- **Makefile identical to master** preserves the `thirdparty` target
  the grader uses to compile their own test binaries against our
  libc.
- **`/etc/rc` strip of `/bin/runtests`** drops the boot-time test
  loop; the grader doesn't want our test harness running on boot.
  `init` itself stays as-is — the loop lives in `/bin/runtests` and
  that binary is removed by the `bin/` strip in step 5.
- **No PR for release branches** because the grader expects a
  submission snapshot, not a feature stream.
