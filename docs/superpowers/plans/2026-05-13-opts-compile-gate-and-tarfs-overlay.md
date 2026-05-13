# OPTS Compile-Gate and Tarfs Overlay — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Vendor Open POSIX Test Suite locally as a gap-discovery tool. Build a Makefile that fetches the suite, compiles every test against our libc, produces a prioritised gap list, and optionally installs linkable tests into the kernel via an opt-in tarfs overlay (`OPTS=1 make thirdparty`). Engineer reads the gap list and drives libc/kernel fixes off it. Phase 2 (Sortix) is a later effort.

**Architecture:** All vendored sources and build artifacts live under `thirdparty/open-posix/` (gitignored). One Makefile drives `fetch` → `opts` (compile+link loop) → `gap-list` (clusters errors). A bash script (`scripts/cluster-errors.sh`) buckets the error log by regex. An `install` target is no-op unless `OPTS=1` is set; when set, it copies BUILD-OK binaries into `build/rootfs/bin/optsbin/` and forces tarfs rebuild. A small in-tree launcher `bin/opts_run/` runs each binary on the kernel and prints a PTS_* tally.

**Tech Stack:** Make, bash/sed/awk, riscv64-unknown-elf-gcc, OPTS 1.5.2 from SourceForge.

**Reference spec:** `docs/superpowers/specs/2026-05-13-external-posix-test-suite-adoption-design.md`

**Working branch:** `feat/external-posix-tests-spec` (already cut off develop; spec lives there).

---

## Task 1: Initialize directory layout and gitignore

**Files:**
- Create: `thirdparty/open-posix/.gitkeep`
- Create: `thirdparty/open-posix/scripts/.gitkeep`
- Create: `thirdparty/open-posix/sanity/.gitkeep`
- Create: `thirdparty/.gitignore`

- [ ] **Step 1: Create the directory skeleton**

```bash
mkdir -p thirdparty/open-posix/{scripts,sanity}
touch thirdparty/open-posix/.gitkeep \
      thirdparty/open-posix/scripts/.gitkeep \
      thirdparty/open-posix/sanity/.gitkeep
```

- [ ] **Step 2: Write thirdparty/.gitignore**

Create `thirdparty/.gitignore` with this exact content:

```gitignore
# Vendored external suites — sources fetched on demand, build artifacts
# are throwaway. Both directories must never be committed.
open-posix/upstream/
open-posix/build/
sortix-regress/upstream/
sortix-regress/build/
```

- [ ] **Step 3: Verify git ignores the right paths**

Run: `mkdir -p thirdparty/open-posix/{upstream,build}/dummy && git status --short thirdparty/open-posix/ | grep -v gitkeep`

Expected: only `.gitkeep` files show up; no `upstream/` or `build/` entries. Then clean up: `rm -rf thirdparty/open-posix/upstream thirdparty/open-posix/build`.

- [ ] **Step 4: Commit**

```bash
git add thirdparty/.gitignore \
        thirdparty/open-posix/.gitkeep \
        thirdparty/open-posix/scripts/.gitkeep \
        thirdparty/open-posix/sanity/.gitkeep
git commit -m "feat(thirdparty): skeleton for open-posix gap-discovery suite

Empty directory layout + gitignore for vendored sources and build
artifacts. No build glue yet — that's the next task."
```

---

## Task 2: Write Makefile with fetch target

**Files:**
- Create: `thirdparty/open-posix/Makefile`

- [ ] **Step 1: Write the Makefile**

Create `thirdparty/open-posix/Makefile` with this exact content:

```makefile
# Open POSIX Test Suite (OPTS) — gap-discovery build glue.
#
# Targets:
#   fetch       Download + verify + unpack OPTS into upstream/.
#   opts        Compile + try-link every test under upstream/ against the
#               in-tree libc; write per-TU outcome to build/results.txt.
#   gap-list    Cluster errors in results.txt into build/gap-list.md.
#   install     OPTS=1 only: copy BUILD-OK binaries to $(ROOTFS)/optsbin/
#               and force tarfs rebuild. No-op without OPTS=1.
#   clean       rm build/ and the optsbin/ rootfs overlay.

ROOTDIR ?= $(abspath $(CURDIR)/../..)
ROOTFS  ?= $(ROOTDIR)/build/rootfs/bin
UPSTREAM := $(CURDIR)/upstream
BUILD := $(CURDIR)/build

# Pinned tarball — OPTS 1.5.2 release on SourceForge.
OPTS_VERSION := 1.5.2
OPTS_URL := https://sourceforge.net/projects/posixtest/files/posixtest/posixtestsuite-$(OPTS_VERSION)/posixtestsuite-$(OPTS_VERSION).tar.gz/download
OPTS_SHA256 :=

# Cross-tooling matches root Makefile:9 exactly, with two changes:
#   - drop -Werror (we want compile errors, not warning-stops)
#   - add -I$(UPSTREAM)/include for posixtest.h
CROSS   := riscv64-unknown-elf-
CC      := $(CROSS)gcc
CFLAGS  := -gdwarf-4 -Wall -Os -ffreestanding -fno-builtin -nostdlib \
           -nostdinc -isystem $(ROOTDIR)/libc/include \
           -mcmodel=medany -march=rv64imac_zicsr_zifencei -mabi=lp64 \
           -ffunction-sections -fdata-sections -fno-tree-switch-conversion

# Linker components (mirrors the rule for bin/* in root Makefile):
CRT0    := $(ROOTDIR)/build/libc/crt.S.o
LIBC    := $(ROOTDIR)/build/libc.a
LDFLAGS := -Wl,--gc-sections

.PHONY: fetch opts gap-list install clean help

help:
	@echo "thirdparty/open-posix targets: fetch opts gap-list install clean"
	@echo "Set OPTS=1 to enable install. Default install is a no-op."

# ---------------------------------------------------------------------------
# Fetch
# ---------------------------------------------------------------------------
fetch: $(UPSTREAM)/.fetched

$(UPSTREAM)/.fetched:
	@mkdir -p $(BUILD)
	@if [ -z "$(OPTS_SHA256)" ]; then \
	  echo "OPTS_SHA256 is empty. Running fetch in bootstrap mode."; \
	  echo "  Downloading $(OPTS_URL) to /tmp/opts.tar.gz..."; \
	  curl -fL -o /tmp/opts.tar.gz "$(OPTS_URL)"; \
	  sha=$$(sha256sum /tmp/opts.tar.gz | awk '{print $$1}'); \
	  echo ""; \
	  echo "  Computed sha256: $$sha"; \
	  echo "  Pin this value into OPTS_SHA256 in $(CURDIR)/Makefile, commit, and re-run 'make fetch'."; \
	  exit 1; \
	fi
	@echo "Fetching OPTS $(OPTS_VERSION)..."
	@curl -fL -o /tmp/opts.tar.gz "$(OPTS_URL)"
	@echo "$(OPTS_SHA256)  /tmp/opts.tar.gz" | sha256sum -c -
	@mkdir -p $(UPSTREAM)
	@tar -xzf /tmp/opts.tar.gz -C $(UPSTREAM) --strip-components=1
	@touch $(UPSTREAM)/.fetched
	@echo "OPTS unpacked into $(UPSTREAM)"

# ---------------------------------------------------------------------------
# Opts compile + link loop  (added in Task 4)
# ---------------------------------------------------------------------------
opts:
	@echo "opts target stubbed; will be filled in Task 4"
	@exit 1

# ---------------------------------------------------------------------------
# Cluster errors into gap-list  (added in Task 6)
# ---------------------------------------------------------------------------
gap-list:
	@echo "gap-list target stubbed; will be filled in Task 6"
	@exit 1

# ---------------------------------------------------------------------------
# Install (dispatcher entry point; no-op unless OPTS=1)
# ---------------------------------------------------------------------------
install:
ifeq ($(OPTS),1)
	@echo "install target stubbed for OPTS=1 path; will be filled in Task 7"
	@exit 1
else
	@echo "thirdparty/open-posix: install is a no-op without OPTS=1"
endif

# ---------------------------------------------------------------------------
clean:
	rm -rf $(BUILD)
	rm -rf $(ROOTFS)/optsbin
```

- [ ] **Step 2: Verify Makefile parses**

Run: `make -C thirdparty/open-posix help`

Expected output:
```
thirdparty/open-posix targets: fetch opts gap-list install clean
Set OPTS=1 to enable install. Default install is a no-op.
```

- [ ] **Step 3: Verify install no-op behavior**

Run: `make -C thirdparty/open-posix install`

Expected output: `thirdparty/open-posix: install is a no-op without OPTS=1`

- [ ] **Step 4: Verify dispatcher doesn't break**

Run: `make thirdparty` from repo root.

Expected: completes successfully (will include "thirdparty: building open-posix" and the no-op install message).

- [ ] **Step 5: Commit**

```bash
git add thirdparty/open-posix/Makefile
git commit -m "feat(thirdparty/open-posix): Makefile skeleton + fetch target

fetch downloads pinned OPTS 1.5.2 tarball from SourceForge, verifies
sha256, unpacks into upstream/. Bootstrap mode prints the computed
sha when OPTS_SHA256 is empty so the engineer can pin it.

opts, gap-list, install targets are stubs that exit 1 (or no-op for
install without OPTS=1) — populated in subsequent tasks."
```

---

## Task 3: Bootstrap the sha256 pin

**Files:**
- Modify: `thirdparty/open-posix/Makefile` (one line)

- [ ] **Step 1: Run fetch in bootstrap mode**

Run: `make -C thirdparty/open-posix fetch`

Expected: downloads the tarball, prints the computed sha256, then exits 1 with instruction to pin the value.

- [ ] **Step 2: Pin the sha256**

Edit `thirdparty/open-posix/Makefile`: replace the line `OPTS_SHA256 :=` with `OPTS_SHA256 := <value-printed-in-step-1>` (the actual hex digest, no quotes).

- [ ] **Step 3: Re-run fetch and verify it unpacks**

Run: `make -C thirdparty/open-posix fetch`

Expected: sha256sum verification passes, tarball unpacks into `thirdparty/open-posix/upstream/`. The directory should contain `conformance/`, `functional/`, `stress/`, `include/`, etc.

Verify:
```bash
ls thirdparty/open-posix/upstream/conformance/interfaces | head -5
```
Expected: directory names like `accept`, `aio_cancel`, `aio_error`, `alarm`, etc.

- [ ] **Step 4: Verify idempotence**

Run: `make -C thirdparty/open-posix fetch` again.

Expected: no-op (the `.fetched` sentinel exists and the rule is satisfied).

- [ ] **Step 5: Commit**

```bash
git add thirdparty/open-posix/Makefile
git commit -m "feat(thirdparty/open-posix): pin OPTS 1.5.2 sha256

Computed from a clean SourceForge download. fetch now verifies the
tarball before unpacking and is idempotent on the .fetched sentinel."
```

---

## Task 4: Implement the opts compile+link loop

**Files:**
- Modify: `thirdparty/open-posix/Makefile` (replace the `opts:` stub)

- [ ] **Step 1: Replace the opts stub**

In `thirdparty/open-posix/Makefile`, find the section labelled `# Opts compile + link loop` and replace the `opts:` rule (everything from `opts:` to the line before `# ---` for gap-list) with this:

```makefile
opts: $(UPSTREAM)/.fetched $(CRT0) $(LIBC)
	@mkdir -p $(BUILD)/bin $(BUILD)/obj $(BUILD)/err
	@: > $(BUILD)/results.txt
	@count=0; ok=0; bf=0; lf=0; \
	for src in $$(find $(UPSTREAM) -name '*.c' \( -path '*/conformance/*' -o -path '*/functional/*' -o -path '*/stress/*' \) 2>/dev/null); do \
	  rel=$${src#$(UPSTREAM)/}; \
	  origin=$$(echo $$rel | cut -d/ -f1); \
	  tag=$$(echo $$rel | sed 's|/|_|g; s|\.c$$||'); \
	  count=$$((count+1)); \
	  if $(CC) $(CFLAGS) -I$(UPSTREAM)/include -c $$src -o $(BUILD)/obj/$$tag.o 2> $(BUILD)/err/$$tag.compile.err; then \
	    if $(CC) $(CFLAGS) -I$(UPSTREAM)/include $(CRT0) $(BUILD)/obj/$$tag.o $(LIBC) $(LDFLAGS) -o $(BUILD)/bin/$$tag 2> $(BUILD)/err/$$tag.link.err; then \
	      ok=$$((ok+1)); \
	      echo "BUILD-OK    [$$origin] $$rel" >> $(BUILD)/results.txt; \
	    else \
	      lf=$$((lf+1)); \
	      first=$$(head -1 $(BUILD)/err/$$tag.link.err); \
	      echo "LINK-FAIL   [$$origin] $$rel :: $$first" >> $(BUILD)/results.txt; \
	    fi; \
	  else \
	    bf=$$((bf+1)); \
	    first=$$(head -1 $(BUILD)/err/$$tag.compile.err); \
	    echo "BUILD-FAIL  [$$origin] $$rel :: $$first" >> $(BUILD)/results.txt; \
	  fi; \
	done; \
	echo ""; \
	echo "opts: $$count TUs scanned: $$ok build-ok, $$bf build-fail, $$lf link-fail"; \
	echo "  results: $(BUILD)/results.txt"
```

Key design notes (the script will reference these — don't modify them):
- Walks `conformance/`, `functional/`, `stress/` subtrees explicitly (skips other dirs like `include/`, `tools/`).
- Tags origin as the top-level subdir.
- One `.err` file per TU per stage (compile, link). Never bails.
- Records exactly one line per TU in `results.txt`.

- [ ] **Step 2: Pre-condition — root libc must be built**

Before running opts, ensure the root `build/libc.a` and `build/libc/crt.S.o` exist:

```bash
make build/libc.a build/libc/crt.S.o
```

Expected: builds succeed.

- [ ] **Step 3: Run opts**

Run: `make -C thirdparty/open-posix opts`

Expected: takes minutes (thousands of TUs). Eventually prints a summary line of the form `opts: N TUs scanned: A build-ok, B build-fail, C link-fail`.

If the loop crashes or hangs:
- Hang: likely a single TU's compile is taking forever (uncommon for OPTS). Identify with `ls -la build/obj/` to see what was last written.
- Crash: the loop is bash-driven; a bad TU name with special chars could break the shell expansion. Look at the last `results.txt` line.

- [ ] **Step 4: Verify results.txt format**

Run: `head -10 thirdparty/open-posix/build/results.txt`

Expected: lines like:
```
BUILD-FAIL  [conformance] conformance/interfaces/aio_cancel/1-1.c :: fatal error: aio.h: No such file or directory
BUILD-FAIL  [conformance] conformance/interfaces/pthread_create/1-1.c :: fatal error: pthread.h: No such file or directory
BUILD-OK    [conformance] conformance/interfaces/open/2-1.c
...
```

- [ ] **Step 5: Commit**

```bash
git add thirdparty/open-posix/Makefile
git commit -m "feat(thirdparty/open-posix): opts target — compile+link gate

Walks conformance/functional/stress subtrees, attempts -c then full
link against in-tree libc per TU, records BUILD-OK/BUILD-FAIL/LINK-FAIL
plus first error line into build/results.txt. Never bails on error."
```

---

## Task 5: Write the sanity smoke test

**Files:**
- Create: `thirdparty/open-posix/sanity/known_gap.c`
- Create: `thirdparty/open-posix/sanity/known_clean.c`
- Modify: `thirdparty/open-posix/Makefile` (add `sanity` target)

- [ ] **Step 1: Write known_gap.c**

Create `thirdparty/open-posix/sanity/known_gap.c`:

```c
/* Smoke-test input for the compile gate. This file MUST fail to compile
 * against our libc because <aio.h> is intentionally out of scope.
 * If the gate reports BUILD-OK for this file, the gate is broken. */
#include <aio.h>

int main(void) { return 0; }
```

- [ ] **Step 2: Write known_clean.c**

Create `thirdparty/open-posix/sanity/known_clean.c`:

```c
/* Smoke-test input for the compile gate. This file MUST compile cleanly
 * (and may even link) against our libc. If the gate reports BUILD-FAIL,
 * the gate or the libc has regressed. */
#include <unistd.h>

int main(void) {
    (void)getpid();
    return 0;
}
```

- [ ] **Step 3: Add sanity target to Makefile**

In `thirdparty/open-posix/Makefile`, add this rule below the `opts:` rule:

```makefile
# Smoke test: runs the same compile+link loop against ./sanity/.
# Used to verify the gate machinery itself is working.
sanity: $(CRT0) $(LIBC)
	@mkdir -p $(BUILD)/bin $(BUILD)/obj $(BUILD)/err
	@: > $(BUILD)/sanity.txt
	@for src in $(CURDIR)/sanity/*.c; do \
	  rel=sanity/$$(basename $$src); \
	  tag=$$(basename $$src .c); \
	  if $(CC) $(CFLAGS) -I$(UPSTREAM)/include -c $$src -o $(BUILD)/obj/sanity_$$tag.o 2> $(BUILD)/err/sanity_$$tag.compile.err; then \
	    if $(CC) $(CFLAGS) -I$(UPSTREAM)/include $(CRT0) $(BUILD)/obj/sanity_$$tag.o $(LIBC) $(LDFLAGS) -o $(BUILD)/bin/sanity_$$tag 2> $(BUILD)/err/sanity_$$tag.link.err; then \
	      echo "BUILD-OK    [sanity] $$rel" >> $(BUILD)/sanity.txt; \
	    else \
	      first=$$(head -1 $(BUILD)/err/sanity_$$tag.link.err); \
	      echo "LINK-FAIL   [sanity] $$rel :: $$first" >> $(BUILD)/sanity.txt; \
	    fi; \
	  else \
	    first=$$(head -1 $(BUILD)/err/sanity_$$tag.compile.err); \
	    echo "BUILD-FAIL  [sanity] $$rel :: $$first" >> $(BUILD)/sanity.txt; \
	  fi; \
	done
	@cat $(BUILD)/sanity.txt
```

Add `sanity` to the `.PHONY` line.

- [ ] **Step 4: Run sanity target**

Run: `make -C thirdparty/open-posix sanity`

Expected output (exact lines):
```
BUILD-FAIL  [sanity] sanity/known_gap.c :: <some path>/aio.h: No such file or directory
BUILD-OK    [sanity] sanity/known_clean.c
```
(The "BUILD-OK" line may also be "LINK-FAIL" if `getpid` isn't yet linkable; that's a libc gap and is acceptable for this smoke. The important assertion is that `known_gap.c` fails with `aio.h` and `known_clean.c` does not fail at the compile stage.)

- [ ] **Step 5: Commit**

```bash
git add thirdparty/open-posix/sanity/known_gap.c \
        thirdparty/open-posix/sanity/known_clean.c \
        thirdparty/open-posix/Makefile
git commit -m "feat(thirdparty/open-posix): sanity smoke test for the gate

Two-file fixture under sanity/: one MUST-FAIL on <aio.h>, one MUST
compile cleanly. make sanity runs the gate logic against just these
files for a quick integration health check."
```

---

## Task 6: Write the cluster-errors script and gap-list target

**Files:**
- Create: `thirdparty/open-posix/scripts/cluster-errors.sh`
- Modify: `thirdparty/open-posix/Makefile` (replace `gap-list:` stub)

- [ ] **Step 1: Write the cluster script**

Create `thirdparty/open-posix/scripts/cluster-errors.sh` (mark executable after):

```bash
#!/usr/bin/env bash
# cluster-errors.sh — bucket build errors from results.txt by regex.
#
# Reads results.txt on stdin or as $1, emits a two-section markdown
# report on stdout:
#   ## Fix         — clusters we should close in libc/kernel
#   ## Out-of-scope — clusters we explicitly will not address
#
# Add/edit clusters by appending to the BUCKETS arrays below.
# Each entry: pattern|label
# Patterns are extended regex matched against the full results.txt line.

set -euo pipefail
in=${1:-/dev/stdin}

# Fix clusters: things that look like real gaps in libc/kernel.
declare -a FIX_BUCKETS=(
  "sys/time\.h: No such file|missing header sys/time.h"
  "sys/mman\.h: No such file|missing header sys/mman.h"
  "dirent\.h: No such file|missing header dirent.h"
  "implicit declaration of function 'strdup'|missing decl strdup"
  "implicit declaration of function 'malloc'|missing decl malloc"
  "implicit declaration of function 'sigaction'|missing decl sigaction"
  "'sigset_t' undeclared|missing type sigset_t"
  "'CLOCK_[A-Z_]+' undeclared|missing CLOCK_* constants"
  "'SA_[A-Z_]+' undeclared|missing SA_* sigaction flags"
  "'_Noreturn' has not been declared|missing _Noreturn"
  "implicit declaration of function 'getpid'|missing decl getpid"
  "implicit declaration of function 'chdir'|missing decl chdir"
  "implicit declaration of function 'getcwd'|missing decl getcwd"
)

# Out-of-scope clusters: things we explicitly will not address.
declare -a OOS_BUCKETS=(
  "pthread\.h: No such file|pthread (no threading)"
  "aio\.h: No such file|AIO"
  "mqueue\.h: No such file|message queues"
  "implicit declaration of function 'pthread_|pthread (no threading)"
  "implicit declaration of function 'aio_|AIO"
  "implicit declaration of function 'mq_|message queues"
  "implicit declaration of function 'sched_|realtime scheduling"
  "implicit declaration of function 'clock_nanosleep'|realtime timers"
  "implicit declaration of function 'sem_|POSIX semaphores"
  "'pthread_[a-zA-Z_]+' undeclared|pthread (no threading)"
  "'PTHREAD_[A-Z_]+' undeclared|pthread (no threading)"
)

count_bucket() {
  # awk avoids the grep -c quirk where "no match" still prints "0" and
  # returns 1; under set -e that's a footgun. awk always succeeds and
  # always prints exactly one integer.
  local pat=$1
  awk -v p="$pat" '$0 ~ p { c++ } END { print c+0 }' "$in"
}

emit_section() {
  local title=$1; shift
  local -a buckets=("$@")
  echo "## $title"
  echo
  local label count
  declare -a rows
  for entry in "${buckets[@]}"; do
    pat=${entry%%|*}
    label=${entry#*|}
    count=$(count_bucket "$pat")
    if [ "$count" -gt 0 ]; then
      rows+=("$count|$label")
    fi
  done
  if [ ${#rows[@]} -eq 0 ]; then
    echo "_(no clusters matched)_"
    echo
    return
  fi
  printf '%s\n' "${rows[@]}" | sort -t '|' -k1,1 -nr | \
    awk -F '|' 'BEGIN{print "| TUs | Cluster |"; print "|----:|---------|"} {printf "| %d | %s |\n", $1, $2}'
  echo
}

total=$(wc -l < "$in")
ok=$(awk '/^BUILD-OK/ { c++ } END { print c+0 }' "$in")
bf=$(awk '/^BUILD-FAIL/ { c++ } END { print c+0 }' "$in")
lf=$(awk '/^LINK-FAIL/ { c++ } END { print c+0 }' "$in")

cat <<EOF
# OPTS gap-list — generated $(date -u +%Y-%m-%dT%H:%M:%SZ)

**Summary.** Total TUs: $total — BUILD-OK $ok, BUILD-FAIL $bf, LINK-FAIL $lf.

EOF

emit_section "Fix" "${FIX_BUCKETS[@]}"
emit_section "Out-of-scope" "${OOS_BUCKETS[@]}"

cat <<EOF
---

_Edit \`thirdparty/open-posix/scripts/cluster-errors.sh\` to add new
buckets as you discover them. Counts only reflect clusters whose
pattern matched at least once; everything else is currently un-bucketed
and lives in \`build/results.txt\`._
EOF
```

- [ ] **Step 2: Make it executable**

Run: `chmod +x thirdparty/open-posix/scripts/cluster-errors.sh`

- [ ] **Step 3: Replace the gap-list stub in the Makefile**

In `thirdparty/open-posix/Makefile`, find the `gap-list:` stub and replace with:

```makefile
gap-list: $(BUILD)/results.txt
	@$(CURDIR)/scripts/cluster-errors.sh $(BUILD)/results.txt > $(BUILD)/gap-list.md
	@echo "Wrote $(BUILD)/gap-list.md"

$(BUILD)/results.txt:
	@echo "$(BUILD)/results.txt is missing. Run 'make opts' first."
	@exit 1
```

- [ ] **Step 4: Test gap-list against sanity output**

First produce a known input:

```bash
make -C thirdparty/open-posix sanity
cp thirdparty/open-posix/build/sanity.txt thirdparty/open-posix/build/results.txt
make -C thirdparty/open-posix gap-list
cat thirdparty/open-posix/build/gap-list.md
```

Expected: the report shows `1` TU under `## Out-of-scope` for AIO. The `## Fix` section may be empty if `known_clean.c` produced BUILD-OK.

- [ ] **Step 5: Test gap-list against full opts output**

```bash
# Restore the real results.txt
make -C thirdparty/open-posix opts
make -C thirdparty/open-posix gap-list
less thirdparty/open-posix/build/gap-list.md
```

Eyeball check: the Out-of-scope section should be dominated by pthread/AIO clusters (probably hundreds of TUs each). The Fix section should list real libc gaps with non-trivial counts. If both sections are empty, the buckets aren't matching — adjust regexes in `cluster-errors.sh` based on actual error lines in `results.txt`.

- [ ] **Step 6: Commit**

```bash
git add thirdparty/open-posix/scripts/cluster-errors.sh thirdparty/open-posix/Makefile
git commit -m "feat(thirdparty/open-posix): gap-list — cluster errors into report

scripts/cluster-errors.sh buckets results.txt lines by regex into Fix
and Out-of-scope sections, sorted by TU count. Bucket-config is
inline in the script and editable as new patterns surface.

gap-list target depends on build/results.txt — fails fast if make opts
hasn't been run."
```

---

## Task 7: Implement install (OPTS=1 path) with tarfs rebuild trigger

**Files:**
- Modify: `thirdparty/open-posix/Makefile` (replace the `install:` ifeq body)

- [ ] **Step 1: Replace the install ifeq body**

In `thirdparty/open-posix/Makefile`, find the `ifeq ($(OPTS),1)` body inside `install:` and replace the stub with:

```makefile
ifeq ($(OPTS),1)
	@if [ ! -d $(BUILD)/bin ] || [ -z "$$(ls -A $(BUILD)/bin 2>/dev/null)" ]; then \
	  echo "thirdparty/open-posix: $(BUILD)/bin is empty; run 'make opts' first."; \
	  exit 1; \
	fi
	@mkdir -p $(ROOTFS)/optsbin
	@cp $(BUILD)/bin/* $(ROOTFS)/optsbin/
	@n=$$(ls -1 $(ROOTFS)/optsbin | wc -l); \
	  echo "thirdparty/open-posix: installed $$n binaries into $(ROOTFS)/optsbin"
	@# Force tarfs rebuild: the root Makefile's build/tarfs.o rule
	@# depends on $(USER_BIN), and our new optsbin/ files aren't in
	@# that list. Removing tarfs artifacts makes the next 'make'
	@# rebuild tarfs and pick up optsbin/.
	@rm -f $(ROOTDIR)/build/tarfs.o $(ROOTDIR)/build/rootfs.tar
```

- [ ] **Step 2: Verify install no-op without OPTS=1**

Run: `make -C thirdparty/open-posix install`

Expected: `thirdparty/open-posix: install is a no-op without OPTS=1` and exit 0.

- [ ] **Step 3: Verify install errors out if opts not run**

```bash
rm -rf thirdparty/open-posix/build/bin
OPTS=1 make -C thirdparty/open-posix install
```

Expected: errors with `thirdparty/open-posix: $(BUILD)/bin is empty; run 'make opts' first.` and exit 1.

- [ ] **Step 4: Run the full workflow**

```bash
make -C thirdparty/open-posix opts          # rebuild build/bin/
OPTS=1 make -C thirdparty/open-posix install
```

Expected: prints `installed N binaries into <rootfs>/optsbin`. Verify:

```bash
ls build/rootfs/bin/optsbin | head -5
test ! -f build/tarfs.o && echo "tarfs.o correctly removed"
test ! -f build/rootfs.tar && echo "rootfs.tar correctly removed"
```

Expected: lists ~50–200 binaries (depending on libc state), both tarfs files report as removed.

- [ ] **Step 5: Verify dispatcher propagates OPTS=1**

```bash
make clean
OPTS=1 make thirdparty
```

Expected: thirdparty dispatcher iterates ports; for open-posix, runs install which sees OPTS=1 and installs.

Note: the dispatcher does NOT automatically run `make opts` first. Engineer must run `make -C thirdparty/open-posix opts` separately before `OPTS=1 make thirdparty`. This is by design (opts is slow and shouldn't run on every dispatch).

- [ ] **Step 6: Commit**

```bash
git add thirdparty/open-posix/Makefile
git commit -m "feat(thirdparty/open-posix): install — opt-in tarfs overlay

OPTS=1 path copies build/bin/* into build/rootfs/bin/optsbin/ and
removes build/tarfs.o + build/rootfs.tar so the next kernel build
re-rolls tarfs with the new optsbin/ entries.

Errors out cleanly if make opts hasn't been run. Without OPTS=1, the
target is a no-op so the dispatcher can iterate harmlessly under a
default make thirdparty."
```

---

## Task 8: Write the opts_run launcher

**Files:**
- Create: `bin/opts_run/opts_run.c`

- [ ] **Step 1: Write the launcher**

Create `bin/opts_run/opts_run.c`:

```c
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>

/* OPTS exit-code conventions (posixtest.h):
 *   0 = PASS, 1 = FAIL, 2 = UNRESOLVED, 4 = UNSUPPORTED, 5 = UNTESTED.
 * Any other non-zero status is treated as FAIL. */
enum { PTS_PASS = 0, PTS_FAIL = 1, PTS_UNRESOLVED = 2,
       PTS_UNSUPPORTED = 4, PTS_UNTESTED = 5 };

static unsigned long pass, fail, unresolved, unsupported, untested, other;

static const char *label(int status) {
    if (!WIFEXITED(status)) return "FAIL  (signaled)";
    switch (WEXITSTATUS(status)) {
        case PTS_PASS:        pass++;        return "PASS";
        case PTS_FAIL:        fail++;        return "FAIL";
        case PTS_UNRESOLVED:  unresolved++;  return "UNRESOLVED";
        case PTS_UNSUPPORTED: unsupported++; return "UNSUPPORTED";
        case PTS_UNTESTED:    untested++;    return "UNTESTED";
        default:              other++;       return "FAIL  (other)";
    }
}

static void run_one(const char *path) {
    int pid = fork();
    if (pid == 0) {
        char *const argv[] = { (char *)path, NULL };
        execv(path, argv);
        _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    printf("%-40s %s\n", path, label(status));
}

static void walk(const char *dir) {
    int fd = open(dir, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "opts_run: cannot open %s\n", dir);
        return;
    }
    /* getdents-style scan via libc dirent. */
    DIR *d = fdopendir(fd);
    if (!d) { close(fd); return; }
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char path[256];
        int n = snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        if (n <= 0 || n >= (int)sizeof path) continue;
        struct stat st;
        if (stat(path, &st) < 0) continue;
        if (S_ISDIR(st.st_mode)) {
            walk(path);
        } else if (S_ISREG(st.st_mode)) {
            run_one(path);
        }
    }
    closedir(d);
}

int main(int argc, char **argv) {
    const char *root = (argc > 1) ? argv[1] : "/bin/optsbin";
    printf("=== opts_run %s ===\n", root);
    walk(root);
    printf("=== Totals ===\n");
    printf("PASS:        %lu\n", pass);
    printf("FAIL:        %lu\n", fail);
    printf("UNRESOLVED:  %lu\n", unresolved);
    printf("UNSUPPORTED: %lu\n", unsupported);
    printf("UNTESTED:    %lu\n", untested);
    printf("OTHER:       %lu\n", other);
    return (fail == 0 && other == 0) ? 0 : 1;
}
```

- [ ] **Step 2: Check that opts_run will build under the default wildcard rule**

The root Makefile has:
```
C_BIN := $(addprefix build/rootfs/bin/,$(shell ls -d bin/*/*.c 2>/dev/null | cut -d/ -f2 | sort -u))
```

So any `bin/<name>/*.c` is automatically picked up. Verify with:

```bash
ls bin/opts_run
```

Expected: shows `opts_run.c`.

- [ ] **Step 3: Build the kernel and verify opts_run is in tarfs**

```bash
make build/rootfs/bin/opts_run
```

Expected: produces the ELF at `build/rootfs/bin/opts_run` without errors.

If it fails due to missing libc symbols (`fdopendir`, `readdir`, `closedir`, `stat`):
- These are POSIX dirent / sys/stat declarations. Run `grep -r "fdopendir\|readdir\|closedir" libc/include/` to see if they're declared.
- If they're missing from libc, **that itself is a gap that opts will surface** — but for the launcher to build, we may need a thin stub. If missing, the simplest workaround is to switch the loop to use `getdents` syscall directly (look at how `bin/ls/ls.c` does it).
- Add a follow-up task to libc to declare them properly. For now, mirror whatever `bin/ls/` does for directory traversal.

- [ ] **Step 4: Smoke-test opts_run in QEMU**

```bash
make clean
make -C thirdparty/open-posix opts
OPTS=1 make thirdparty
make qemu
```

At the shell prompt:
```
opts_run /bin/optsbin
```

Expected: each binary runs in sequence, each prints a PASS/FAIL/UNRESOLVED/... line, ends with a Totals block. Exit shell with `exit`, then `Ctrl-A X` to exit QEMU.

If `opts_run` reports `cannot open /bin/optsbin`, the install didn't run or tarfs didn't rebuild — re-verify task 7 step 4.

- [ ] **Step 5: Commit**

```bash
git add bin/opts_run/opts_run.c
git commit -m "feat(bin): opts_run — PTS_*-aware launcher for OPTS binaries

Recursively walks /bin/optsbin (default) or argv[1], execs each
regular file, maps exit status to PTS_PASS/FAIL/UNRESOLVED/...
categories, prints a per-test result line and a totals block.

Not added to bin/init/init.c — we don't want OPTS running at boot;
invoke manually from the shell. Always built (not gated on OPTS=1)
because the binary is tiny and runs whether or not optsbin/ is
populated."
```

---

## Task 9: End-to-end workflow verification

**Files:**
- None modified; this task is verification only.

- [ ] **Step 1: Clean slate**

```bash
make clean
rm -rf thirdparty/open-posix/build
```

- [ ] **Step 2: Full default build (no OPTS)**

```bash
make
```

Expected: kernel.elf builds normally. `build/rootfs/bin/` should NOT have an `optsbin/` subdir:

```bash
test ! -d build/rootfs/bin/optsbin && echo "optsbin correctly absent under default make"
```

- [ ] **Step 3: Run default kernel and confirm /bin/optsbin doesn't exist**

```bash
printf ' ls /bin/optsbin\nexit\n' | timeout 60 make -s qemu > /tmp/run.log 2>&1
grep -E "optsbin|No such" /tmp/run.log
```

Expected: an error like "No such file or directory" — confirms optsbin/ is not in the default kernel.

- [ ] **Step 4: Opt-in build with OPTS**

```bash
make -C thirdparty/open-posix opts
OPTS=1 make thirdparty
make
```

Expected:
- `make thirdparty` reports OPTS install with a binary count.
- `make` rebuilds tarfs (because install removed `build/tarfs.o`) and produces a new kernel.elf.

- [ ] **Step 5: Run opts_run in QEMU**

```bash
printf ' opts_run /bin/optsbin\nexit\n' | timeout 600 make -s qemu > /tmp/opts-run.log 2>&1
grep -E "PASS:|FAIL:|UNRESOLVED:|UNSUPPORTED:|UNTESTED:" /tmp/opts-run.log
```

Expected: a Totals block with at least some PASS and FAIL counts. The exact numbers depend on libc state. Numbers will move as you close gaps from `gap-list.md`.

- [ ] **Step 6: Verify make submit excludes OPTS**

```bash
SUBMIT_DIR=/tmp/sbunix-submit-test make submit
find /tmp/sbunix-submit-test -path '*optsbin*' -o -path '*open-posix*' 2>/dev/null
ls /tmp/sbunix-submit-test
```

Expected: the `find` command produces no output (optsbin/ and open-posix/ both absent from the submission). The submission tree contains the source tree minus `build/`, `thirdparty/`, etc. Clean up: `rm -rf /tmp/sbunix-submit-test`.

- [ ] **Step 7: Commit (verification notes only, no code changes expected)**

If steps 1–6 all passed, no commit is needed. If any step revealed a fix, commit it with `fix(thirdparty/open-posix): <what>` as the subject. If you fixed something, re-run steps 4–6 to re-verify.

---

## Task 10: First gap-list triage and document

**Files:**
- Create: `thirdparty/open-posix/build/gap-list.md` (generated, gitignored — but engineer reviews it)
- Modify (optional): `thirdparty/open-posix/scripts/cluster-errors.sh` if additional patterns surface

- [ ] **Step 1: Generate the gap-list**

```bash
make -C thirdparty/open-posix gap-list
cat thirdparty/open-posix/build/gap-list.md
```

- [ ] **Step 2: Inspect the Fix section**

Look at the top 3–5 clusters in the Fix section. Cross-reference with `evalmessages_compiled.txt` and `submission_audit_findings.md`. For each cluster, note whether:
- the grader has already cited it (high priority)
- the audit has already cited it (high priority)
- it's new and unrecognized (medium priority — investigate before fixing)

- [ ] **Step 3: Inspect the most common error lines**

```bash
grep -E "^BUILD-FAIL|^LINK-FAIL" thirdparty/open-posix/build/results.txt | \
  awk -F '::' '{print $2}' | sort | uniq -c | sort -nr | head -20
```

Expected: top 20 error messages by frequency. Compare against the bucket patterns in `cluster-errors.sh`. If a common error doesn't match any current bucket, add a new entry to either `FIX_BUCKETS` or `OOS_BUCKETS` (whichever is appropriate) and re-run `make gap-list` to verify the new bucket picks them up.

- [ ] **Step 4: Stop here**

No commit at the end of this task unless you added new bucket patterns to the script. The Fix list itself drives subsequent libc/kernel work, which is **out of scope for this plan** — that work happens on separate feature branches per cluster.

The plan ends here. From this point on, the workflow is:
1. Pick a Fix cluster from `gap-list.md`.
2. Cut a feature branch off `develop`.
3. Implement the libc/kernel change.
4. Re-run `make -C thirdparty/open-posix opts gap-list`.
5. Verify the cluster's TU count drops (or the cluster disappears).
6. Open a PR.

---

## Out-of-scope (for this plan)

- Sortix regress suite vendoring (Phase 2 of the spec). Separate plan when phase 1 gaps are triaged.
- Any libc or kernel fix driven by the gap list. Each fix is its own feature branch and PR.
- Integrating opts into CI or `init.c`'s boot-time test list.
- Multi-disk virtio support (would have been required for the original separate-VirtIO-disk design).
