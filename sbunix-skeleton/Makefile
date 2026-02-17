KLANG ?= c

CROSS   := riscv64-unknown-elf-
CC      := $(CROSS)gcc
LD      := $(CROSS)ld
AR      := $(CROSS)ar
OBJCOPY := $(CROSS)objcopy
CFLAGS  := -Wall -Werror -ffreestanding -nostdlib -nostdinc -mcmodel=medany -march=rv64imac_zicsr_zifencei -mabi=lp64

KERN_ASM := $(patsubst %,build/%.o,$(wildcard kernel/*.S))
LIBC_OBJ := $(patsubst %,build/%.o,$(filter-out libc/crt.S,$(wildcard libc/*.c libc/*.S)))
USER_BIN := $(patsubst bin/%,build/rootfs/bin/%,$(wildcard bin/*))

# Language-specific kernel object/library
ifeq ($(KLANG),c)
  KERN_C   := $(patsubst %,build/%.o,$(wildcard kernel/*.c))
  KERN_ALL := $(KERN_ASM) $(KERN_C)
else ifeq ($(KLANG),rust)
  KERN_LIB := build/rust/riscv64imac-unknown-none-elf/debug/libsbunix.a
  KERN_ALL := $(KERN_ASM) $(KERN_LIB)
else ifeq ($(KLANG),zig)
  KERN_LIB := zig-out/lib/libsbunix.a
  KERN_ALL := $(KERN_ASM) $(KERN_LIB)
endif

DISK_SIZE ?= 16
DISK_IMG  := build/disk.img

all: build/kernel.elf

build/%.c.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -I$(<D)/include -c $< -o $@

build/%.S.o: %.S
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -I$(<D)/include -c $< -o $@

build/libc.a: $(LIBC_OBJ)
	@mkdir -p $(@D)
	$(AR) rcs $@ $^

# Rust kernel build
build/rust/riscv64imac-unknown-none-elf/debug/libsbunix.a: $(wildcard kernel/*.rs) Cargo.toml
	cargo build

# Zig kernel build
zig-out/lib/libsbunix.a: $(wildcard kernel/*.zig) build.zig
	zig build -Doptimize=ReleaseSafe

.SECONDEXPANSION:
build/rootfs/bin/%: $$(wildcard bin/%/*.c) build/libc/crt.S.o build/libc.a
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -Ilibc/include build/libc/crt.S.o bin/$*/*.c build/libc.a -o $@

build/tarfs.o: $(USER_BIN)
	cp -a rootfs/. build/rootfs/
	tar cf build/rootfs.tar -C build/rootfs .
	cd build && $(OBJCOPY) -I binary -O elf64-littleriscv -B riscv \
		--rename-section .data=.tarfs \
		--redefine-sym _binary_rootfs_tar_start=_tarfs_start \
		--redefine-sym _binary_rootfs_tar_end=_tarfs_end \
		--redefine-sym _binary_rootfs_tar_size=_tarfs_size \
		rootfs.tar tarfs.o

build/kernel.elf: $(KERN_ALL) build/tarfs.o
	$(LD) -T kernel/kernel.ld $(KERN_ALL) build/tarfs.o -o $@

thirdparty: build/libc/crt.S.o build/libc.a
	$(MAKE) -C thirdparty busybox-install
	@rm -f build/tarfs.o
	$(MAKE) build/kernel.elf

build/tools/mkfs: tools/mkfs.c
	@mkdir -p $(@D)
	gcc -Wall -o $@ $<

$(DISK_IMG): build/tools/mkfs
	build/tools/mkfs $@ $(DISK_SIZE)

qemu: build/kernel.elf $(DISK_IMG)
	qemu-system-riscv64 -machine virt -bios default -kernel $< \
		-drive file=$(DISK_IMG),format=raw,if=none,id=hd0 \
		-device virtio-blk-pci-non-transitional,drive=hd0 \
		-device virtio-gpu-pci \
		-device virtio-net-pci-non-transitional,netdev=net0 \
		-netdev user,id=net0 \
		-nographic

clean:
	rm -rf build zig-out .zig-cache

.PRECIOUS: build/%.S.o build/%.c.o
.PHONY: all qemu clean thirdparty
