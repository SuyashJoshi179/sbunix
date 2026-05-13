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
OPTS_SHA256 := 15a2185672127cba851d35ec9d538ff6148defdbb75f99c7e9c50aeba0f94757

# Cross-tooling matches root Makefile:9 exactly, with two changes:
#   - drop -Werror (we want compile errors, not warning-stops)
#   - add -I$(UPSTREAM)/include for posixtest.h
CROSS   := riscv64-unknown-elf-
CC      := $(CROSS)gcc
CFLAGS  := -gdwarf-4 -Wall -Os -ffreestanding -fno-builtin -nostdlib \
           -nostdinc -isystem $(shell $(CC) -print-file-name=include) \
           -isystem $(ROOTDIR)/libc/include \
           -mcmodel=medany -march=rv64imac_zicsr_zifencei -mabi=lp64 \
           -ffunction-sections -fdata-sections -fno-tree-switch-conversion

# Linker components (mirrors the rule for bin/* in root Makefile):
CRT0    := $(ROOTDIR)/build/libc/crt.S.o
LIBC    := $(ROOTDIR)/build/libc.a
LDFLAGS := -Wl,--gc-sections

.PHONY: fetch opts gap-list install clean help sanity

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

# ---------------------------------------------------------------------------
# Cluster errors into gap-list  (added in Task 6)
# ---------------------------------------------------------------------------
gap-list: $(BUILD)/results.txt
	@$(CURDIR)/scripts/cluster-errors.sh $(BUILD)/results.txt > $(BUILD)/gap-list.md
	@echo "Wrote $(BUILD)/gap-list.md"

$(BUILD)/results.txt:
	@echo "$(BUILD)/results.txt is missing. Run 'make opts' first."
	@exit 1

# ---------------------------------------------------------------------------
# Install (dispatcher entry point; no-op unless OPTS=1)
# ---------------------------------------------------------------------------
install:
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
else
	@echo "thirdparty/open-posix: install is a no-op without OPTS=1"
endif

# ---------------------------------------------------------------------------
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

# ---------------------------------------------------------------------------
clean:
	rm -rf $(BUILD)
