//! Tell the bootloader to load new code.

use core::cell::Cell;
use kernel::common::take_cell::TakeCell;
use kernel::common::take_cell::MapCell;
use kernel::hil;
use kernel::{AppId, AppSlice, Driver, ReturnCode, Shared};

/// This module is either waiting to do something, or handling a read/write.
#[derive(Clone,Copy,Debug,PartialEq)]
enum State {}

pub struct SignpostTockFirmwareUpdate<'a, F: hil::flash::Flash + 'static> {
    /// The module providing a `Flash` interface.
    driver: &'a F,
    config: MapCell<AppSlice<Shared, u8>>,
    pagebuffer: TakeCell<'static, F::Page>,
    source: Cell<u32>,
    destination: Cell<u32>,
    length: Cell<u32>,
    crc: Cell<u32>,
}

impl<'a, F: hil::flash::Flash + 'a> SignpostTockFirmwareUpdate<'a, F> {
    pub fn new(driver: &'a F, buffer: &'static mut F::Page) -> SignpostTockFirmwareUpdate<'a, F> {
        SignpostTockFirmwareUpdate {
            driver: driver,
            config: MapCell::empty(),
            pagebuffer: TakeCell::new(buffer),
            source: Cell::new(0),
            destination: Cell::new(0),
            length: Cell::new(0),
            crc: Cell::new(0),
        }
    }

    fn do_firmware_update (&self, source: u32, destination: u32, length: u32, crc: u32) -> ReturnCode {
        self.pagebuffer.take().map_or(ReturnCode::ERESERVE, move |pagebuffer| {
            self.source.set(source);
            self.destination.set(destination);
            self.length.set(length);
            self.crc.set(crc);

            debug!("a");

            // Second page has the bootloader flags on it.
            self.driver.read_page(2, pagebuffer)
        })
    }
}



impl<'a, F: hil::flash::Flash + 'a> hil::flash::Client<F> for SignpostTockFirmwareUpdate<'a, F> {
    fn read_complete(&self, pagebuffer: &'static mut F::Page, _error: hil::flash::Error) {
        // Put the correct values in the correct spots.

        pagebuffer.as_mut()[492] = 1; // enable
        pagebuffer.as_mut()[493] = 0;
        pagebuffer.as_mut()[494] = 0;
        pagebuffer.as_mut()[495] = 0;
        pagebuffer.as_mut()[496] = ((self.source.get() >>  0) & 0xFF) as u8;
        pagebuffer.as_mut()[497] = ((self.source.get() >>  8) & 0xFF) as u8;
        pagebuffer.as_mut()[498] = ((self.source.get() >> 16) & 0xFF) as u8;
        pagebuffer.as_mut()[499] = ((self.source.get() >> 24) & 0xFF) as u8;
        pagebuffer.as_mut()[500] = ((self.destination.get() >>  0) & 0xFF) as u8;
        pagebuffer.as_mut()[501] = ((self.destination.get() >>  8) & 0xFF) as u8;
        pagebuffer.as_mut()[502] = ((self.destination.get() >> 16) & 0xFF) as u8;
        pagebuffer.as_mut()[503] = ((self.destination.get() >> 24) & 0xFF) as u8;
        pagebuffer.as_mut()[504] = ((self.length.get() >>  0) & 0xFF) as u8;
        pagebuffer.as_mut()[505] = ((self.length.get() >>  8) & 0xFF) as u8;
        pagebuffer.as_mut()[506] = ((self.length.get() >> 16) & 0xFF) as u8;
        pagebuffer.as_mut()[507] = ((self.length.get() >> 24) & 0xFF) as u8;
        pagebuffer.as_mut()[508] = ((self.crc.get() >>  0) & 0xFF) as u8;
        pagebuffer.as_mut()[509] = ((self.crc.get() >>  8) & 0xFF) as u8;
        pagebuffer.as_mut()[510] = ((self.crc.get() >> 16) & 0xFF) as u8;
        pagebuffer.as_mut()[511] = ((self.crc.get() >> 24) & 0xFF) as u8;

        self.driver.write_page(2, pagebuffer);
    }

    fn write_complete(&self, _pagebuffer: &'static mut F::Page, _error: hil::flash::Error) {
        // Reboot!
    }

    fn erase_complete(&self, _error: hil::flash::Error) {}
}

/// Provide an interface for userland.
impl<'a, F: hil::flash::Flash + 'a> Driver for SignpostTockFirmwareUpdate<'a, F> {
    /// Setup buffer for passing settings in.
    ///
    /// ### `allow_num`
    ///
    /// - `0`: Buffer that is 16 bytes long and will contain reset config information.
    fn allow(&self, _appid: AppId, _allow_num: usize, slice: Option<AppSlice<Shared, u8>>) -> ReturnCode {
        let s = slice.unwrap();
        if s.len() == 16 {
            self.config.replace(s);
            ReturnCode::SUCCESS
        } else {
            ReturnCode::EINVAL
        }
    }

    /// Command interface.
    ///
    /// ### `command_num`
    ///
    /// - `0`: Return SUCCESS if this driver is included on the platform.
    /// - `1`: Do it.
    fn command(&self, command_num: usize, _arg1: usize, _: usize, _appid: AppId) -> ReturnCode {

        match command_num {
            0 => /* This driver exists. */ ReturnCode::SUCCESS,

            // Do it.
            1 => {
                self.config.map_or(ReturnCode::ERESERVE, |buffer| {
                    let mut count = 0;
                    let mut params: [u32; 4] = [0, 0, 0, 0];
                    for chunk in buffer.chunks_mut(4) {
                        params[count] = (chunk[0] as u32) << 0 |
                                        (chunk[1] as u32) << 8 |
                                        (chunk[2] as u32) << 16 |
                                        (chunk[3] as u32) << 24;
                        count += 1;
                    }
                    self.do_firmware_update(params[0], params[1], params[2], params[3])
                })
            }

            _ => ReturnCode::ENOSUPPORT,
        }
    }
}
