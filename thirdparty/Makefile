TOP     := ..
CROSS   := riscv64-unknown-elf-
CC      := $(CROSS)gcc
HOSTCC  := gcc
GCC_INC := $(shell $(CC) -print-file-name=include)
CFLAGS  := -Wall -Werror -ffreestanding -nostdlib -nostdinc -mcmodel=medany \
           -march=rv64imac_zicsr_zifencei -mabi=lp64 \
           -isystem $(abspath .)/include -isystem $(abspath $(TOP)/libc/include) -isystem $(GCC_INC) -D__sbunix__ \
           -Wno-misleading-indentation -Wno-unused-function

# Probe student libc (without our shim's -isystem) to see which POSIX types
# and functions they already provide. Emit -DHAVE_<NAME> so the shim can
# skip duplicates.
PROBE_CC := $(CC) -ffreestanding -nostdlib -nostdinc -mcmodel=medany \
            -march=rv64imac_zicsr_zifencei -mabi=lp64 -D__sbunix__ \
            -Werror=implicit-function-declaration \
            -isystem $(abspath $(TOP)/libc/include) -isystem $(GCC_INC)
probe_type = $(if $(shell printf '#include <sys/types.h>\n#include <time.h>\n$(1) __p;\n' \
    | $(PROBE_CC) -x c -c -o /dev/null - 2>/dev/null && echo y),-DHAVE_$(shell echo $(1) | tr a-z A-Z))
probe_func = $(if $(shell printf '#include <sys/types.h>\n#include <sys/time.h>\n#include <unistd.h>\n#include <time.h>\n#include <signal.h>\n#include <string.h>\nvoid __t(void){(void)$(1);}\n' \
    | $(PROBE_CC) -x c -c -o /dev/null - 2>/dev/null && echo y),-D$(or $(2),HAVE_$(shell echo $(1) | tr a-z A-Z)))
PROBE_DEFS := $(call probe_type,clock_t) \
              $(call probe_type,useconds_t) \
              $(call probe_type,suseconds_t) \
              $(call probe_func,settimeofday) \
              $(call probe_func,usleep) \
              $(call probe_func,strsignal,SBU_HAS_STRSIGNAL)
CFLAGS += $(PROBE_DEFS)
DUMMY_OBJ    := $(abspath $(TOP)/build/thirdparty/dummy.o)
COMPAT_OBJ   := $(abspath $(TOP)/build/thirdparty/compat.o)
SETJMP_OBJ   := $(abspath $(TOP)/build/thirdparty/setjmp.o)
SIGSET_FALLBACK_OBJ := $(abspath $(TOP)/build/thirdparty/sigset_fallback.o)
SOFTFLOAT_OBJ := $(abspath $(TOP)/build/thirdparty/softfloat.o)
BB_PRINTF_OBJ := $(abspath $(TOP)/build/thirdparty/bb_printf.o)
LIBGCC       := $(shell $(CC) -print-libgcc-file-name)
LIBC_THIRDPARTY := $(abspath $(TOP)/build/thirdparty/libc-thirdparty.o)

# Pre-link compat + dummy + setjmp + libc.a into a single relocatable object,
# resolving compat-vs-student stdio duplicates here (compat first → wins) so
# downstream final links never see multiple definitions. This is robust to
# build systems (e.g. busybox kbuild) that strip -Wl,* flags from LDFLAGS.
$(LIBC_THIRDPARTY): $(COMPAT_OBJ) $(DUMMY_OBJ) $(SETJMP_OBJ) $(SIGSET_FALLBACK_OBJ) $(TOP)/build/libc.a
	@mkdir -p $(@D)
	$(CROSS)ld -r --allow-multiple-definition -o $@ \
	    $(COMPAT_OBJ) $(DUMMY_OBJ) $(SETJMP_OBJ) $(SIGSET_FALLBACK_OBJ) \
	    --whole-archive $(abspath $(TOP)/build/libc.a) --no-whole-archive

LDFLAGS := -nostdlib $(abspath $(TOP)/build/libc/crt.S.o) \
           $(LIBC_THIRDPARTY) \
           -Wl,--allow-multiple-definition -Wl,--gc-sections -Wl,--no-relax

$(DUMMY_OBJ): dummy.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections -c $< -o $@

$(COMPAT_OBJ): compat.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections -c $< -o $@

$(SETJMP_OBJ): setjmp.S
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(SIGSET_FALLBACK_OBJ): sigset_fallback.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections -c $< -o $@

$(SOFTFLOAT_OBJ): softfloat.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections -c $< -o $@

$(BB_PRINTF_OBJ): bb_printf.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections -c $< -o $@

# --- Busybox ---

busybox busybox-install busybox-menuconfig: export CROSS_COMPILE=$(CROSS)
busybox busybox-install busybox-menuconfig: export EXTRA_CFLAGS=$(CFLAGS) -Wno-format
busybox busybox-install busybox-menuconfig: export EXTRA_LDFLAGS=$(LDFLAGS) $(BB_PRINTF_OBJ)

busybox-patch:
	@cd busybox && git checkout -q -- include/platform.h include/libbb.h libbb/Kbuild.src scripts/Makefile.lib 2>/dev/null || true
	@cd busybox && git apply ../busybox-sbunix.patch

busybox: busybox-patch $(LIBC_THIRDPARTY) $(BB_PRINTF_OBJ)
	@mkdir -p $(abspath ../build/busybox)
	@test -f $(abspath ../build/busybox/.config) || cp busybox_defconfig $(abspath ../build/busybox/.config)
	@yes '' | $(MAKE) -C busybox O=$(abspath ../build/busybox) oldconfig >/dev/null 2>&1 || true
	$(MAKE) -C busybox O=$(abspath ../build/busybox) SKIP_STRIP=y

busybox-install: busybox
	$(MAKE) -C busybox O=$(abspath ../build/busybox) \
		CONFIG_PREFIX=$(abspath ../build/rootfs) install

busybox-menuconfig:
	$(MAKE) -C busybox O=$(abspath ../build/busybox) menuconfig

# --- MicroPython ---

PYTHON_DIR    := python/ports/unix
PYTHON_CFLAGS := -ffreestanding -fno-builtin -nostdlib -nostdinc -mcmodel=medany \
                 -march=rv64imac_zicsr_zifencei -mabi=lp64 \
                 -isystem $(abspath .)/include -isystem $(abspath $(TOP)/libc/include) -isystem $(GCC_INC) -D__sbunix__ \
                 -DMICROPY_NLR_SETJMP=1 -DMICROPY_NO_ALLOCA=1 -Wno-error \
                 -D_DIRENT_HAVE_D_TYPE=1
PYTHON_CFLAGS += $(PROBE_DEFS)
PYTHON_LDFLAGS := -nostdlib $(abspath $(TOP)/build/libc/crt.S.o) \
                  $(LIBC_THIRDPARTY) \
                  -Wl,--allow-multiple-definition -Wl,--no-warn-mismatch -Wl,--no-relax -lgcc

python-patch:
	@cd python && git checkout -q -- ports/unix/mpconfigport.h 2>/dev/null || true
	@cd python && git apply ../python-sbunix.patch

python: python-patch $(LIBC_THIRDPARTY)
	$(MAKE) -C $(PYTHON_DIR) VARIANT=minimal \
		CROSS_COMPILE=$(CROSS) \
		CFLAGS_EXTRA="$(PYTHON_CFLAGS)" \
		LDFLAGS_EXTRA="$(PYTHON_LDFLAGS)" \
		LDFLAGS_ARCH="-Wl,-Map=build-minimal/micropython.map,--cref -Wl,--gc-sections"

python-install: python
	@mkdir -p $(abspath ../build/rootfs/bin)
	cp $(PYTHON_DIR)/build-minimal/micropython $(abspath ../build/rootfs/bin/python)
	@mkdir -p $(abspath ../build/rootfs/home)
	cp python-examples/* $(abspath ../build/rootfs/home/)

python-clean:
	$(MAKE) -C $(PYTHON_DIR) VARIANT=minimal clean

# --- TCC (Tiny C Compiler) ---

TCC_DIR   := tcc
TCC_BUILD := $(abspath ../build/tcc)
TCC_CFLAGS := $(CFLAGS) -Wno-implicit-function-declaration \
              -DONE_SOURCE=1 -DCONFIG_TCC_STATIC=1 -DTCC_TARGET_RISCV64 \
              -DCONFIG_TCC_PREDEFS=1 \
              -I $(TCC_DIR) -I $(TCC_DIR)/include -I $(TCC_BUILD)
TCC_LDFLAGS := $(LDFLAGS) $(SOFTFLOAT_OBJ) -Wl,--no-warn-mismatch $(LIBGCC)

$(TCC_BUILD)/tccdefs_.h: $(TCC_DIR)/include/tccdefs.h $(TCC_DIR)/conftest.c
	@mkdir -p $(TCC_BUILD)
	$(HOSTCC) -DC2STR $(TCC_DIR)/conftest.c -o $(TCC_BUILD)/c2str
	$(TCC_BUILD)/c2str $< $@
	@rm -f $(TCC_BUILD)/c2str

TCC_LIB_SRC := lib-arm64.c builtin.c dsohandle.c stdatomic.c
TCC_LIB_ASM := atomic.S
TCC_LIB_OBJ := $(addprefix $(TCC_BUILD)/lib/,$(TCC_LIB_SRC:.c=.o) $(TCC_LIB_ASM:.S=.o))
TCC_LIB_CFLAGS := -ffreestanding -nostdlib -nostdinc -mcmodel=medany \
                  -march=rv64imac_zicsr_zifencei -mabi=lp64 \
                  -isystem $(abspath .)/include -isystem $(abspath $(TOP)/libc/include) -isystem $(GCC_INC) \
                  -D__sbunix__ -Wno-unused-function $(PROBE_DEFS)

$(TCC_BUILD)/lib/%.o: $(TCC_DIR)/lib/%.c
	@mkdir -p $(@D)
	$(CC) $(TCC_LIB_CFLAGS) -c $< -o $@

$(TCC_BUILD)/lib/%.o: $(TCC_DIR)/lib/%.S
	@mkdir -p $(@D)
	$(CC) $(TCC_LIB_CFLAGS) -c $< -o $@

$(TCC_BUILD)/libtcc1.a: $(TCC_LIB_OBJ)
	$(CROSS)ar rcs $@ $^

tcc: $(LIBC_THIRDPARTY) $(SOFTFLOAT_OBJ) $(TCC_BUILD)/tccdefs_.h $(TCC_BUILD)/libtcc1.a
	@mkdir -p $(TCC_BUILD)
	$(CC) $(TCC_CFLAGS) -c $(TCC_DIR)/tcc.c -o $(TCC_BUILD)/tcc.o
	$(CC) $(TCC_BUILD)/tcc.o $(TCC_LDFLAGS) -o $(TCC_BUILD)/cc

tcc-install: tcc
	@mkdir -p $(abspath ../build/rootfs/bin) $(abspath ../build/rootfs/lib/tcc) $(abspath ../build/rootfs/lib/tcc/include)
	@mkdir -p $(abspath ../build/rootfs/include/sys)
	cp $(TCC_BUILD)/cc $(abspath ../build/rootfs/bin/cc)
	cp $(TCC_BUILD)/libtcc1.a $(abspath ../build/rootfs/lib/tcc/)
	cp $(TCC_DIR)/include/*.h $(TCC_DIR)/tcclib.h $(abspath ../build/rootfs/lib/tcc/include/)
	cp $(abspath $(TOP)/libc/include)/*.h $(abspath ../build/rootfs/include/)
	cp $(abspath $(TOP)/libc/include/sys)/*.h $(abspath ../build/rootfs/include/sys/)
	cp $(abspath $(TOP)/build/libc/crt.S.o) $(abspath ../build/rootfs/lib/crt.o)
	cp $(abspath $(TOP)/build/libc.a) $(abspath ../build/rootfs/lib/libc.a)
	@mkdir -p $(abspath ../build/rootfs/home)
	cp tcc-examples/* $(abspath ../build/rootfs/home/)

# --- pdpmake ---

MAKE_DIR    := make
MAKE_SRC    := $(wildcard $(MAKE_DIR)/*.c)
MAKE_OBJ    := $(addprefix $(TOP)/build/thirdparty/make/,$(notdir $(MAKE_SRC:.c=.o)))
MAKE_CFLAGS := $(CFLAGS) -Wno-unused-function -Wno-unused-variable

$(TOP)/build/thirdparty/make/%.o: $(MAKE_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(MAKE_CFLAGS) -c $< -o $@

pdpmake: $(LIBC_THIRDPARTY) $(MAKE_OBJ)
	$(CC) $(MAKE_OBJ) $(LDFLAGS) -o $(TOP)/build/thirdparty/make/make

pdpmake-install: pdpmake
	@mkdir -p $(abspath ../build/rootfs/bin)
	cp $(TOP)/build/thirdparty/make/make $(abspath ../build/rootfs/bin/make)

# --- Auto-discover and install all available packages ---
# Override with: THIRDPARTY_PKGS="busybox tcc" make -C thirdparty install

ifdef THIRDPARTY_PKGS
  PACKAGES := $(THIRDPARTY_PKGS)
else
  PACKAGES := $(notdir $(wildcard busybox python tcc make))
endif
INSTALL_TARGETS := $(foreach p,$(PACKAGES),$(if $(filter make,$(p)),pdpmake-install,$(p)-install))

install: $(INSTALL_TARGETS)

testsuite:
	$(MAKE) -C testsuite all
	@rm -f $(TOP)/build/tarfs.o
	$(MAKE) -C $(TOP) build/kernel.elf

test:
	python3 testsuite/run_tests.py

.PHONY: install testsuite test busybox busybox-patch busybox-install busybox-menuconfig
.PHONY: python python-patch python-install python-clean
.PHONY: tcc tcc-install
.PHONY: pdpmake pdpmake-install
