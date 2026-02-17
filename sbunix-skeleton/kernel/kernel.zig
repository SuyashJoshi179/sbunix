const printk = @import("printk.zig");

export fn boot() callconv(.c) void {
    printk.printk("Booting SBUnix\n");
}
