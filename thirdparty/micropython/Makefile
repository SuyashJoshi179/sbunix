# thirdparty/micropython: dispatcher invoked by thirdparty/Makefile.
#
# `install` builds mpy-cross on the host, then builds the SBUnix port
# under port/, then copies the resulting ELF into $(ROOTFS)/micropython.
#
# ROOTDIR (repo root) and ROOTFS (= $(ROOTDIR)/build/rootfs/bin) are
# passed in by ../Makefile.

UPSTREAM    := $(CURDIR)/upstream
PORT        := $(CURDIR)/port
MPY_VERSION := 1.22.2
MPY_TARBALL := https://github.com/micropython/micropython/archive/refs/tags/v$(MPY_VERSION).tar.gz

.PHONY: install clean fetch mpy-cross port

install: port

# Fetch upstream on demand so the repo stays small (per ../README.md).
fetch:
	@if [ ! -d "$(UPSTREAM)" ]; then \
	    echo "thirdparty/micropython: fetching v$(MPY_VERSION)"; \
	    curl -fL -o $(CURDIR)/upstream.tar.gz $(MPY_TARBALL); \
	    tar xzf $(CURDIR)/upstream.tar.gz -C $(CURDIR); \
	    mv $(CURDIR)/micropython-$(MPY_VERSION) $(UPSTREAM); \
	    rm $(CURDIR)/upstream.tar.gz; \
	fi

mpy-cross: fetch
	@if [ ! -x "$(UPSTREAM)/mpy-cross/build/mpy-cross" ]; then \
	    echo "thirdparty/micropython: building mpy-cross"; \
	    $(MAKE) -C $(UPSTREAM)/mpy-cross; \
	fi

port: mpy-cross
	@echo "thirdparty/micropython: building SBUnix port"
	$(MAKE) -C $(PORT) install ROOTDIR=$(ROOTDIR) ROOTFS=$(ROOTFS)

clean:
	$(MAKE) -C $(PORT) clean-port || true
	$(MAKE) -C $(UPSTREAM)/mpy-cross clean || true
