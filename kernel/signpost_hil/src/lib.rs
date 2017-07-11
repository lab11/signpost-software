#![crate_name = "signpost_hil"]
#![crate_type = "rlib"]
#![feature(asm,lang_items,const_fn)]
#![no_std]

extern crate kernel;

pub mod i2c_selector;
