const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.resolveTargetQuery(.{
        .cpu_arch = .riscv64,
        .cpu_model = .{ .explicit = &std.Target.riscv.cpu.generic_rv64 },
        .cpu_features_add = std.Target.riscv.featureSet(&.{
            .i,
            .m,
            .a,
            .c,
        }),
        .os_tag = .freestanding,
        .abi = .none,
    });

    const optimize = b.standardOptimizeOption(.{});

    const lib = b.addStaticLibrary(.{
        .name = "sbunix",
        .root_source_file = b.path("kernel/kernel.zig"),
        .target = target,
        .optimize = optimize,
    });

    lib.root_module.red_zone = false;
    lib.root_module.code_model = .medany;
    lib.root_module.stack_check = false;
    lib.root_module.stack_protector = false;
    lib.root_module.unwind_tables = .none;

    b.installArtifact(lib);
}
