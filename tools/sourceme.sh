# CSE 506 - SBUnix Development Environment
# Usage: source tools/sourceme.sh

# RISC-V GCC cross-compiler and QEMU
export PATH="/opt/shared/riscv-toolchain/bin:/opt/shared/qemu/bin:$PATH"

# Rust toolchain
export RUSTUP_HOME="/opt/shared/rust"
export CARGO_HOME="/opt/shared/cargo"
export PATH="/opt/shared/cargo/bin:$PATH"

# Zig compiler
export PATH="/opt/shared/zig:$PATH"
