# SBUnix port of MicroPython.
#
# Driven by ../Makefile (the thirdparty dispatcher target). Inherits
# ROOTDIR (repo root) and ROOTFS (rootfs/bin) via environment.
#
# This Makefile uses upstream's mkenv/py.mk/mkrules.mk machinery so we
# get qstr generation + py-core compilation for free, but overrides CC,
# CFLAGS, and the link rule to target SBUnix's RV64 + in-tree libc.

# Locate upstream relative to this directory.
PORT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
TOP      := $(abspath $(PORT_DIR)/../upstream)

include $(TOP)/py/mkenv.mk

# qstr defs (must come before py.mk).
QSTR_DEFS = qstrdefsport.h

# We are doing a freestanding cross-compile.
CROSS_COMPILE = riscv64-unknown-elf-
CC = $(CROSS_COMPILE)gcc
LD = $(CROSS_COMPILE)gcc
SIZE = $(CROSS_COMPILE)size
AR = $(CROSS_COMPILE)ar
AS = $(CROSS_COMPILE)as

# Disable ROM text compression — saves a moving target during bring-up.
MICROPY_ROM_TEXT_COMPRESSION ?= 0

include $(TOP)/py/py.mk

INC += -I.
INC += -I$(TOP)
INC += -I$(BUILD)
INC += -isystem $(ROOTDIR)/libc/include

# Match the project's user-binary build flags from Makefile:9, with two
# additions: -std=c99 (MicroPython requires it) and -fno-stack-protector
# (we have no __stack_chk_guard).
CFLAGS += $(INC)
CFLAGS += -march=rv64imac_zicsr_zifencei -mabi=lp64 -mcmodel=medany
CFLAGS += -ffreestanding -fno-builtin -nostdlib -nostdinc
CFLAGS += -isystem $(shell $(CC) -print-file-name=include)
CFLAGS += -isystem $(ROOTDIR)/libc/include
CFLAGS += -std=c99 -Wall
CFLAGS += -Os -fdata-sections -ffunction-sections
CFLAGS += -fno-stack-protector -fno-tree-switch-conversion
CFLAGS += -DNDEBUG

# Performance hot path is fine without -O3 here; keep -Os.
CSUPEROPT = -Os

CXXFLAGS += $(filter-out -std=c99,$(CFLAGS))

LIBS = $(ROOTDIR)/build/libc.a

LDFLAGS += -nostdlib -nostartfiles -static
LDFLAGS += -march=rv64imac_zicsr_zifencei -mabi=lp64
LDFLAGS += -Wl,--gc-sections
LDFLAGS += -Wl,-Map=$(BUILD)/micropython.map,--cref

SRC_C = \
	main.c \
	mphalport.c \
	shared/runtime/gchelper_generic.c \
	shared/runtime/stdout_helpers.c \

SRC_QSTR += shared/runtime/stdout_helpers.c

OBJ += $(PY_CORE_O)
OBJ += $(addprefix $(BUILD)/, $(SRC_C:.c=.o))

# CRT (in-tree libc startup) goes first on the link line.
CRT_OBJ = $(ROOTDIR)/build/libc/crt.S.o

all: $(BUILD)/micropython.elf

$(BUILD)/micropython.elf: $(OBJ) $(CRT_OBJ) $(LIBS)
	$(ECHO) "LINK $@"
	$(Q)$(LD) $(LDFLAGS) -o $@ $(CRT_OBJ) $(OBJ) $(LIBS)
	$(Q)$(SIZE) $@

# Copy the final ELF into the rootfs so tarfs picks it up.
install: $(BUILD)/micropython.elf
	@mkdir -p $(ROOTFS)
	cp $< $(ROOTFS)/micropython
	$(ECHO) "INSTALL $(ROOTFS)/micropython"

clean-port:
	rm -rf $(BUILD)

include $(TOP)/py/mkrules.mk
