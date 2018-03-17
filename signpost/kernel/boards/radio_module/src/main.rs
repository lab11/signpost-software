#![crate_name = "radio_module"]
#![no_std]
#![no_main]
#![feature(asm,compiler_builtins_lib,const_fn,lang_items)]

extern crate capsules;
extern crate compiler_builtins;
extern crate cortexm4;
#[macro_use(debug,static_init)]
extern crate kernel;
extern crate sam4l;

extern crate signpost_drivers;
extern crate signpost_hil;

use capsules::console::{self, Console};
use signpost_drivers::sara_u260;
use signpost_drivers::xdot;
use capsules::nrf51822_serialization::{self, Nrf51822Serialization};
use capsules::virtual_alarm::{MuxAlarm, VirtualMuxAlarm};
use kernel::hil;
use kernel::hil::Controller;
use kernel::{Chip, Platform};
use kernel::mpu::MPU;
use sam4l::usart;

// For panic!()
#[macro_use]
pub mod io;
pub mod version;

// Number of concurrent processes this platform supports.
const NUM_PROCS: usize = 2;

// How should the kernel respond when a process faults.
const FAULT_RESPONSE: kernel::process::FaultResponse = kernel::process::FaultResponse::Panic;

#[link_section = ".app_memory"]
static mut APP_MEMORY: [u8; 40960] = [0; 40960];

// Actual memory for holding the active process structures.
static mut PROCESSES: [Option<kernel::Process<'static>>; NUM_PROCS] = [None, None];

/*******************************************************************************
 * Setup this platform
 ******************************************************************************/

struct RadioModule {
    console: &'static Console<'static, usart::USART>,
    lora_console: &'static signpost_drivers::xdot::Console<'static, usart::USART>,
    three_g_console: &'static signpost_drivers::sara_u260::Console<'static, usart::USART>,
    gpio: &'static capsules::gpio::GPIO<'static, sam4l::gpio::GPIOPin>,
    led: &'static capsules::led::LED<'static, sam4l::gpio::GPIOPin>,
    timer: &'static capsules::alarm::AlarmDriver<'static, VirtualMuxAlarm<'static, sam4l::ast::Ast<'static>>>,
    i2c_master_slave: &'static capsules::i2c_master_slave_driver::I2CMasterSlaveDriver<'static>,
    nrf51822: &'static Nrf51822Serialization<'static, usart::USART>,
    app_watchdog: &'static signpost_drivers::app_watchdog::AppWatchdog<'static, VirtualMuxAlarm<'static, sam4l::ast::Ast<'static>>>,
    rng: &'static capsules::rng::SimpleRng<'static, sam4l::trng::Trng<'static>>,
    app_flash: &'static capsules::app_flash_driver::AppFlash<'static>,
    stfu: &'static signpost_drivers::signpost_tock_firmware_update::SignpostTockFirmwareUpdate<'static,
        capsules::virtual_flash::FlashUser<'static, sam4l::flashcalw::FLASHCALW>>,
    stfu_holding: &'static capsules::nonvolatile_storage_driver::NonvolatileStorage<'static>,
    ipc: kernel::ipc::IPC,
}

impl Platform for RadioModule {
    fn with_driver<F, R>(&self, driver_num: usize, f: F) -> R
        where F: FnOnce(Option<&kernel::Driver>) -> R
    {

        match driver_num {
            capsules::console::DRIVER_NUM => f(Some(self.console)),
            capsules::gpio::DRIVER_NUM => f(Some(self.gpio)),
            capsules::alarm::DRIVER_NUM => f(Some(self.timer)),
            capsules::nrf51822_serialization::DRIVER_NUM => f(Some(self.nrf51822)),
            capsules::led::DRIVER_NUM => f(Some(self.led)),
            13 => f(Some(self.i2c_master_slave)),
            capsules::rng::DRIVER_NUM => f(Some(self.rng)),
            capsules::app_flash_driver::DRIVER_NUM => f(Some(self.app_flash)),

            signpost_drivers::app_watchdog::DRIVER_NUM => f(Some(self.app_watchdog)),
            signpost_drivers::xdot::DRIVER_NUM => f(Some(self.lora_console)),
            signpost_drivers::sara_u260::DRIVER_NUM => f(Some(self.three_g_console)),
            signpost_drivers::signpost_tock_firmware_update::DRIVER_NUM => f(Some(self.stfu)),
            signpost_drivers::signpost_tock_firmware_update::DRIVER_NUM2 => f(Some(self.stfu)),

            kernel::ipc::DRIVER_NUM => f(Some(&self.ipc)),
            _ => f(None)
        }
    }
}


unsafe fn set_pin_primary_functions() {
    use sam4l::gpio::{PA,PB};
    use sam4l::gpio::PeripheralFunction::{A, B, C};

    PB[00].configure(Some(A));  //SDA
    PB[01].configure(Some(A));  //SCL
    PB[02].configure(None);     //NRF Reset
    PB[03].configure(None);     //MOD_OUT
    PB[04].configure(None);     //XDOT_RESET
    PB[05].configure(None);     //MOD_IN
    PB[06].configure(Some(A));  //NRF_INT1/CTS
    PB[07].configure(Some(A));  //NRF_INT2/RTS
    PB[08].configure(None);     //NRF Boot
    PB[09].configure(Some(A));  //NRF TX
    PB[10].configure(Some(A));  //NRF RX
    PB[11].configure(None);     //NRF Power Gate
    PB[12].configure(None);     //smbus alert
    PB[13].configure(None);     //XDOT_Int1
    PB[14].configure(Some(C));  //sda for smbus
    PB[15].configure(Some(C));  //scl for smbus


    PA[04].configure(None);     //DBG GPIO1
    PA[05].configure(None);     //DBG GPIO2
    PA[06].configure(None);     //XDOT Power Gate
    PA[07].configure(None);     //3G Power Gate
    PA[08].configure(None);  //3G CTS
    PA[09].configure(Some(A));  //3G RTS
    PA[10].configure(None);     //PPS
    PA[11].configure(Some(A));  //3G Tx
    PA[12].configure(Some(A));  //3G Rx
    PA[13].configure(None);     //3G Reset
    PA[14].configure(None);     //3G Power signal
    PA[15].configure(Some(A));  //DBG rx
    PA[16].configure(Some(A));  //DBG tx
    PA[17].configure(Some(A));  //XDOT CTS
    PA[18].configure(None);     //XDOT WAKE
    PA[19].configure(Some(A));  //xDOT Tx
    PA[20].configure(Some(A));  //XDOT Rx
    PA[21].configure(None);     //LED 1
    PA[22].configure(Some(B));  //XDOT RTS
    PA[23].configure(None);     //LED 2
    PA[24].configure(None);     //LED 3
    PA[25].configure(Some(A));  //USB
    PA[26].configure(Some(A));  //USB


}

/*******************************************************************************
 * Main init function
 ******************************************************************************/

#[no_mangle]
pub unsafe fn reset_handler() {
    sam4l::init();

    sam4l::pm::PM.setup_system_clock(sam4l::pm::SystemClockSource::ExternalOscillator {
        frequency: sam4l::pm::OscillatorFrequency::Frequency16MHz,
        startup_mode: sam4l::pm::OscillatorStartup::SlowStart,
    });

    // Source 32Khz and 1Khz clocks from RC23K (SAM4L Datasheet 11.6.8)
    sam4l::bpm::set_ck32source(sam4l::bpm::CK32Source::RC32K);

    set_pin_primary_functions();

    //
    // UART console
    //
    let console = static_init!(
        Console<usart::USART>,
        Console::new(&usart::USART1,
                     115200,
                     &mut console::WRITE_BUF,
                     kernel::Grant::create()));
    hil::uart::UART::set_client(&usart::USART1, console);

    //
    // LoRa console
    //
    let lora_console = static_init!(
        signpost_drivers::xdot::Console<usart::USART>,
        signpost_drivers::xdot::Console::new(&usart::USART2,
                    115200,
                    &mut xdot::WRITE_BUF,
                    &mut xdot::READ_BUF,
                    kernel::Grant::create()));
    hil::uart::UART::set_client(&usart::USART2, lora_console);

    //
    // 3G console
    //
    let three_g_console = static_init!(
        signpost_drivers::sara_u260::Console<usart::USART>,
        signpost_drivers::sara_u260::Console::new(&usart::USART0,
                    115200,
                    &mut sara_u260::WRITE_BUF,
                    &mut sara_u260::READ_BUF,
                    kernel::Grant::create()));
    hil::uart::UART::set_client(&usart::USART0, three_g_console);



    let nrf_serialization = static_init!(
        Nrf51822Serialization<usart::USART>,
        Nrf51822Serialization::new(&usart::USART3,
                &mut nrf51822_serialization::WRITE_BUF,
                &mut nrf51822_serialization::READ_BUF));
    hil::uart::UART::set_client(&usart::USART3, nrf_serialization);

    //
    // Timer
    //
    let ast = &sam4l::ast::AST;

    let mux_alarm = static_init!(
        MuxAlarm<'static, sam4l::ast::Ast>,
        MuxAlarm::new(&sam4l::ast::AST));
    ast.configure(mux_alarm);

    let virtual_alarm1 = static_init!(
        VirtualMuxAlarm<'static, sam4l::ast::Ast>,
        VirtualMuxAlarm::new(mux_alarm));
    let timer = static_init!(
        capsules::alarm::AlarmDriver<'static, VirtualMuxAlarm<'static, sam4l::ast::Ast>>,
        capsules::alarm::AlarmDriver::new(virtual_alarm1, kernel::Grant::create()));
    virtual_alarm1.set_client(timer);

    // Setup RNG
    let rng = static_init!(
            capsules::rng::SimpleRng<'static, sam4l::trng::Trng>,
            capsules::rng::SimpleRng::new(&sam4l::trng::TRNG, kernel::Grant::create()));
    sam4l::trng::TRNG.set_client(rng);

    //
    // I2C Buses
    //
    let i2c_modules = static_init!(
        capsules::i2c_master_slave_driver::I2CMasterSlaveDriver<'static>,
        capsules::i2c_master_slave_driver::I2CMasterSlaveDriver::new(&sam4l::i2c::I2C1,
            &mut capsules::i2c_master_slave_driver::BUFFER1,
            &mut capsules::i2c_master_slave_driver::BUFFER2,
            &mut capsules::i2c_master_slave_driver::BUFFER3));
    sam4l::i2c::I2C1.set_master_client(i2c_modules);
    sam4l::i2c::I2C1.set_slave_client(i2c_modules);

    hil::i2c::I2CSlave::set_address(&sam4l::i2c::I2C1, 0x22);

    //
    // Remaining GPIO pins
    //
    let gpio_pins = static_init!(
        [&'static sam4l::gpio::GPIOPin; 14],
        [&sam4l::gpio::PB[03], //MOD_OUT
         &sam4l::gpio::PB[05], //MOD_IN
         &sam4l::gpio::PA[10], //PPS
         &sam4l::gpio::PB[02], //NRF RESET
         &sam4l::gpio::PB[08], //NRF BOOT
         &sam4l::gpio::PB[11], //NRF POWERGATE
         &sam4l::gpio::PB[13], //LORA INT1
         &sam4l::gpio::PB[08], //NRF BOOT
         &sam4l::gpio::PA[06], //LORA POWERGATE
         &sam4l::gpio::PB[04], //LORA RESET
         &sam4l::gpio::PA[18], //LORA WAKE
         &sam4l::gpio::PA[07], //GSM POWERGATE
         &sam4l::gpio::PA[13], //GSM RESET
         &sam4l::gpio::PA[14]] //GSM POWER
    );
    let gpio = static_init!(
        capsules::gpio::GPIO<'static, sam4l::gpio::GPIOPin>,
        capsules::gpio::GPIO::new(gpio_pins));
    for pin in gpio_pins.iter() {
        pin.set_client(gpio);
    }

    //
    //LEDs/
    //
    let led_pins = static_init!(
        [(&'static sam4l::gpio::GPIOPin, capsules::led::ActivationMode); 5],
          [(&sam4l::gpio::PA[04], capsules::led::ActivationMode::ActiveHigh), //DBG_GPIO1
           (&sam4l::gpio::PA[05], capsules::led::ActivationMode::ActiveHigh), //DBG_GPIO2
           (&sam4l::gpio::PA[21], capsules::led::ActivationMode::ActiveLow),  // LED1
           (&sam4l::gpio::PA[23], capsules::led::ActivationMode::ActiveLow),  // LED2
           (&sam4l::gpio::PA[24], capsules::led::ActivationMode::ActiveLow)]  // LED3
           );
    let led = static_init!(
        capsules::led::LED<'static, sam4l::gpio::GPIOPin>,
        capsules::led::LED::new(led_pins));

    // configure initial state for debug LEDs
    sam4l::gpio::PA[05].clear(); // red LED off
    sam4l::gpio::PA[04].set();   // green LED on


    //
    // App Watchdog
    //
    let app_timeout_alarm = static_init!(
        VirtualMuxAlarm<'static, sam4l::ast::Ast>,
        VirtualMuxAlarm::new(mux_alarm));
    let kernel_timeout_alarm = static_init!(
        VirtualMuxAlarm<'static, sam4l::ast::Ast>,
        VirtualMuxAlarm::new(mux_alarm));
    let app_timeout = static_init!(
        signpost_drivers::app_watchdog::Timeout<'static, VirtualMuxAlarm<'static, sam4l::ast::Ast>>,
        signpost_drivers::app_watchdog::Timeout::new(app_timeout_alarm, signpost_drivers::app_watchdog::TimeoutMode::App, 1000, cortexm4::scb::reset));
    app_timeout_alarm.set_client(app_timeout);
    let kernel_timeout = static_init!(
        signpost_drivers::app_watchdog::Timeout<'static, VirtualMuxAlarm<'static, sam4l::ast::Ast>>,
        signpost_drivers::app_watchdog::Timeout::new(kernel_timeout_alarm, signpost_drivers::app_watchdog::TimeoutMode::Kernel, 5000, cortexm4::scb::reset));
    kernel_timeout_alarm.set_client(kernel_timeout);
    let app_watchdog = static_init!(
        signpost_drivers::app_watchdog::AppWatchdog<'static, VirtualMuxAlarm<'static, sam4l::ast::Ast>>,
        signpost_drivers::app_watchdog::AppWatchdog::new(app_timeout, kernel_timeout));

    //
    // Kernel Watchdog
    //
    let watchdog_alarm = static_init!(
        VirtualMuxAlarm<'static, sam4l::ast::Ast>,
        VirtualMuxAlarm::new(mux_alarm));
    let watchdog = static_init!(
        signpost_drivers::watchdog_kernel::WatchdogKernel<'static, VirtualMuxAlarm<'static, sam4l::ast::Ast>>,
        signpost_drivers::watchdog_kernel::WatchdogKernel::new(watchdog_alarm, &sam4l::wdt::WDT, 1200));
    watchdog_alarm.set_client(watchdog);

    //
    // Flash
    //

    let mux_flash = static_init!(
        capsules::virtual_flash::MuxFlash<'static, sam4l::flashcalw::FLASHCALW>,
        capsules::virtual_flash::MuxFlash::new(&sam4l::flashcalw::FLASH_CONTROLLER));
    hil::flash::HasClient::set_client(&sam4l::flashcalw::FLASH_CONTROLLER, mux_flash);
    sam4l::flashcalw::FLASH_CONTROLLER.configure();

    //
    // App Flash
    //
    let virtual_flash_app_holding = static_init!(
        capsules::virtual_flash::FlashUser<'static, sam4l::flashcalw::FLASHCALW>,
        capsules::virtual_flash::FlashUser::new(mux_flash));
    pub static mut APP_HOLDING_PAGEBUFFER: sam4l::flashcalw::Sam4lPage = sam4l::flashcalw::Sam4lPage::new();

    let app_holding_nv_to_page = static_init!(
        capsules::nonvolatile_to_pages::NonvolatileToPages<'static,
            capsules::virtual_flash::FlashUser<'static, sam4l::flashcalw::FLASHCALW>>,
        capsules::nonvolatile_to_pages::NonvolatileToPages::new(
            virtual_flash_app_holding,
            &mut APP_HOLDING_PAGEBUFFER));
    hil::flash::HasClient::set_client(virtual_flash_app_holding, app_holding_nv_to_page);

    pub static mut APP_FLASH_BUFFER: [u8; 512] = [0; 512];
    let app_flash = static_init!(
        capsules::app_flash_driver::AppFlash<'static>,
        capsules::app_flash_driver::AppFlash::new(app_holding_nv_to_page,
            kernel::Grant::create(), &mut APP_FLASH_BUFFER));
    hil::nonvolatile_storage::NonvolatileStorage::set_client(app_holding_nv_to_page, app_flash);

    //
    // Firmware Update
    //
    let virtual_flash_stfu_holding = static_init!(
        capsules::virtual_flash::FlashUser<'static, sam4l::flashcalw::FLASHCALW>,
        capsules::virtual_flash::FlashUser::new(mux_flash));
    pub static mut STFU_HOLDING_PAGEBUFFER: sam4l::flashcalw::Sam4lPage = sam4l::flashcalw::Sam4lPage::new();

    let stfu_holding_nv_to_page = static_init!(
        capsules::nonvolatile_to_pages::NonvolatileToPages<'static,
            capsules::virtual_flash::FlashUser<'static, sam4l::flashcalw::FLASHCALW>>,
        capsules::nonvolatile_to_pages::NonvolatileToPages::new(
            virtual_flash_stfu_holding,
            &mut STFU_HOLDING_PAGEBUFFER));
    hil::flash::HasClient::set_client(virtual_flash_stfu_holding, stfu_holding_nv_to_page);

    pub static mut STFU_HOLDING_BUFFER: [u8; 512] = [0; 512];
    let stfu_holding = static_init!(
        capsules::nonvolatile_storage_driver::NonvolatileStorage<'static>,
        capsules::nonvolatile_storage_driver::NonvolatileStorage::new(
            stfu_holding_nv_to_page, kernel::Grant::create(),
            0x60000, // Start address for userspace accessible region
            0x20000, // Length of userspace accessible region
            0,       // Start address of kernel accessible region
            0,       // Length of kernel accessible region
            &mut STFU_HOLDING_BUFFER));
    hil::nonvolatile_storage::NonvolatileStorage::set_client(stfu_holding_nv_to_page, stfu_holding);


    let virtual_flash_btldrflags = static_init!(
        capsules::virtual_flash::FlashUser<'static, sam4l::flashcalw::FLASHCALW>,
        capsules::virtual_flash::FlashUser::new(mux_flash));
    pub static mut BTLDRPAGEBUFFER: sam4l::flashcalw::Sam4lPage = sam4l::flashcalw::Sam4lPage::new();

    let stfu = static_init!(
        signpost_drivers::signpost_tock_firmware_update::SignpostTockFirmwareUpdate<'static,
            capsules::virtual_flash::FlashUser<'static, sam4l::flashcalw::FLASHCALW>>,
        signpost_drivers::signpost_tock_firmware_update::SignpostTockFirmwareUpdate::new(
            virtual_flash_btldrflags,
            &mut BTLDRPAGEBUFFER));
    hil::flash::HasClient::set_client(virtual_flash_btldrflags, stfu);


    //
    // Actual platform object
    //
    let radio_module = RadioModule {
        console: console,
        lora_console: lora_console,
        three_g_console: three_g_console,
        gpio: gpio,
        led: led,
        timer: timer,
        i2c_master_slave: i2c_modules,
        nrf51822:nrf_serialization,
        app_watchdog: app_watchdog,
        rng: rng,
        app_flash: app_flash,
        stfu: stfu,
        stfu_holding: stfu_holding,
        ipc: kernel::ipc::IPC::new(),
    };

    let kc = static_init!(
        capsules::console::App,
        capsules::console::App::default());
    kernel::debug::assign_console_driver(Some(radio_module.console), kc);
    watchdog.start();

    //fix the rts line
    /*sam4l::gpio::PB[06].enable();
    sam4l::gpio::PB[06].enable_output();
    sam4l::gpio::PB[06].clear();*/

    //turn off gsm power
    sam4l::gpio::PA[07].enable();
    sam4l::gpio::PA[07].enable_output();
    sam4l::gpio::PA[07].clear();

    //clear gsm cts
    sam4l::gpio::PA[08].enable();
    sam4l::gpio::PA[08].enable_output();
    sam4l::gpio::PA[08].clear();

    //clear gsm RST
    sam4l::gpio::PA[13].enable();
    sam4l::gpio::PA[13].enable_output();
    sam4l::gpio::PA[13].clear();

    //clear gsm poweron
    sam4l::gpio::PA[14].enable();
    sam4l::gpio::PA[14].enable_output();
    sam4l::gpio::PA[14].clear();

    radio_module.lora_console.initialize();
    radio_module.console.initialize();
    radio_module.three_g_console.initialize();
    radio_module.nrf51822.initialize();
    watchdog.start();

    let mut chip = sam4l::chip::Sam4l::new();
    chip.mpu().enable_mpu();

    debug!("Running {} Version {} from git {}",
           env!("CARGO_PKG_NAME"),
           env!("CARGO_PKG_VERSION"),
           version::GIT_VERSION,
           );

    extern "C" {
        /// Beginning of the ROM region containing app images.
        static _sapps: u8;
    }
    kernel::process::load_processes(&_sapps as *const u8,
                                    &mut APP_MEMORY,
                                    &mut PROCESSES,
                                    FAULT_RESPONSE);

    kernel::main(&radio_module, &mut chip, &mut PROCESSES, &radio_module.ipc);
}
