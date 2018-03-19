#![crate_name = "signpost_drivers"]
#![crate_type = "rlib"]
#![feature(const_fn)]
#![no_std]

#[macro_use(debug)]
extern crate kernel;
extern crate signpost_hil;

pub mod smbus_interrupt;
pub mod app_watchdog;
pub mod watchdog_kernel;

pub mod gps_console;
pub mod sara_u260;
pub mod xdot;

pub mod signpost_tock_firmware_update;
