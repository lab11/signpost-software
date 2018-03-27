use core::fmt::*;
use kernel::hil::uart::{self, UART};
use kernel::debug;
use cortexm4;
use sam4l;

pub struct Writer {
    initialized: bool,
}

pub static mut WRITER: Writer = Writer { initialized: false };

impl Write for Writer {
    fn write_str(&mut self, s: &str) -> ::core::fmt::Result {
        let uart = unsafe { &mut sam4l::usart::USART0 };
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


#[cfg(not(test))]
#[no_mangle]
#[lang="panic_fmt"]
pub unsafe extern "C" fn panic_fmt(args: Arguments, file: &'static str, line: u32) -> ! {

    let writer = &mut WRITER;
    debug::panic_begin();
    debug::panic_banner(writer, args, file, line);
    
    debug::flush(writer);
    debug::panic_process_info(writer);

    cortexm4::scb::reset();

    // blink the panic signal
    let led = &sam4l::gpio::PA[4];
    led.enable_output();
    loop {
        for _ in 0..1000000 {
            led.clear();
        }
        for _ in 0..100000 {
            led.set();
        }
        for _ in 0..1000000 {
            led.clear();
        }
        for _ in 0..500000 {
            led.set();
        }
    }
}
