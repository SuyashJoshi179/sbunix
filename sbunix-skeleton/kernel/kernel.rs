#![no_std]

mod printk;

use core::panic::PanicInfo;

#[no_mangle]
pub extern "C" fn boot() {
    printk::printk("Booting SBUnix\n");
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}
