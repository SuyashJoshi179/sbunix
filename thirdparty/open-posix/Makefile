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
OPTS_URL := https://sourceforge.net/projects/posixtest/files/posixtest/$(OPTS_VERSION)/posixtestsuite-$(OPTS_VERSION).tar.gz/download
OPTS_SHA256 :=

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
