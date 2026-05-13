# Top-level dispatcher for thirdparty/ ports.
#
# Each subdirectory under thirdparty/ is a port. A port is included in the
# build when its directory contains a Makefile with an `install` target that
# drops the resulting binary into ../../build/rootfs/bin/.
#
# Invoked from the top-level Makefile via `make thirdparty`. That target
# builds build/libc.a and build/libc/crt.S.o first, then $(MAKE) -C this
# directory `install`, then re-runs the kernel build so tarfs picks up the
# newly installed binaries.
#
# The dispatcher is content-free: subdirectories *opt in* by having a
# Makefile. Empty placeholder directories (or ones that contain only a
# README) are skipped without error, so a fresh checkout where no port
# has been staged still satisfies `make thirdparty`.

ROOTDIR := $(abspath $(CURDIR)/..)
ROOTFS  := $(ROOTDIR)/build/rootfs/bin

# Subdirs that have an install target. find -mindepth/-maxdepth 1 keeps it
# to direct children; -name 'Makefile' filters out READMEs etc.
PORT_DIRS := $(patsubst %/Makefile,%,$(wildcard */Makefile))

.PHONY: install clean $(PORT_DIRS)

install: $(PORT_DIRS)
	@echo "thirdparty: installed $(words $(PORT_DIRS)) port(s) into $(ROOTFS)"

$(PORT_DIRS):
	@echo "thirdparty: building $@"
	@mkdir -p $(ROOTFS)
	$(MAKE) -C $@ install ROOTDIR=$(ROOTDIR) ROOTFS=$(ROOTFS)

clean:
	@for d in $(PORT_DIRS); do \
	     $(MAKE) -C $$d clean ROOTDIR=$(ROOTDIR) ROOTFS=$(ROOTFS) || true; \
	done
