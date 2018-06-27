use core::fmt::*;
use core::panic::PanicInfo;
use kernel::debug;
use kernel::hil::led;
use kernel::hil::uart::{self, UART};
use sam4l;
use cortexm4;

pub struct Writer {
    initialized: bool,
}

pub static mut WRITER: Writer = Writer { initialized: false };

impl Write for Writer {
    fn write_str(&mut self, s: &str) -> ::core::fmt::Result {
        let uart = unsafe { &mut sam4l::usart::USART1 };
        let regs_manager = &sam4l::usart::USARTRegManager::panic_new(&uart);
        if !self.initialized {
            self.initialized = true;
            uart.init(uart::UARTParams {
                baud_rate: 115200,
                stop_bits: uart::StopBits::One,
                parity: uart::Parity::None,
                hw_flow_control: false,
            });
            uart.enable_tx(regs_manager);

        }
        // XXX: I'd like to get this working the "right" way, but I'm not sure how
        for c in s.bytes() {
            uart.send_byte(regs_manager, c);
            while !uart.tx_ready(regs_manager) {}
        }
        Ok(())
    }
}


/// Panic handler.
#[cfg(not(test))]
#[no_mangle]
#[panic_implementation]
pub unsafe extern "C" fn panic_fmt(pi: &PanicInfo) -> ! {
    let led = &mut led::LedLow::new(&mut sam4l::gpio::PB[11]);

    /* TODO: Used to also blink backplane led
     *
    let backplane_led = &sam4l::gpio::PB[14];
    */

    /* TODO: This was removed
     *
    // Optional reset after hard fault
    for _ in 0..1000000 {
        asm!("nop");
    }
    cortexm4::scb::reset();
    */

    let writer = &mut WRITER;
    debug::panic(led, writer, pi, &cortexm4::support::nop)
}

