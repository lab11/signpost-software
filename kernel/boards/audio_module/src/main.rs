#![crate_name = "audio_module"]
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

// use capsules::console::{self, Console};
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
static mut APP_MEMORY: [u8; 16384*2] = [0; 16384*2];

// Actual memory for holding the active process structures.
static mut PROCESSES: [Option<kernel::Process<'static>>; NUM_PROCS] = [None, None];

/*******************************************************************************
 * Setup this platform
 ******************************************************************************/

struct AudioModule {
    console: &'static capsules::console::Console<'static, usart::USART>,
    gpio: &'static capsules::gpio::GPIO<'static, sam4l::gpio::GPIOPin>,
    led: &'static capsules::led::LED<'static, sam4l::gpio::GPIOPin>,
    alarm: &'static capsules::alarm::AlarmDriver<'static, VirtualMuxAlarm<'static, sam4l::ast::Ast<'static>>>,
    i2c_master_slave: &'static capsules::i2c_master_slave_driver::I2CMasterSlaveDriver<'static>,
    adc: &'static capsules::adc::Adc<'static, sam4l::adc::Adc>,
    app_watchdog: &'static signpost_drivers::app_watchdog::AppWatchdog<'static, VirtualMuxAlarm<'static, sam4l::ast::Ast<'static>>>,
    rng: &'static capsules::rng::SimpleRng<'static, sam4l::trng::Trng<'static>>,
    app_flash: &'static capsules::app_flash_driver::AppFlash<'static>,
    stfu: &'static signpost_drivers::signpost_tock_firmware_update::SignpostTockFirmwareUpdate<'static,
        capsules::virtual_flash::FlashUser<'static, sam4l::flashcalw::FLASHCALW>>,
    stfu_holding: &'static capsules::nonvolatile_storage_driver::NonvolatileStorage<'static>,
    ipc: kernel::ipc::IPC,
}

impl Platform for AudioModule {
    fn with_driver<F, R>(&self, driver_num: usize, f: F) -> R
        where F: FnOnce(Option<&kernel::Driver>) -> R
    {

        match driver_num {
            capsules::console::DRIVER_NUM => f(Some(self.console)),
            capsules::gpio::DRIVER_NUM => f(Some(self.gpio)),
            capsules::alarm::DRIVER_NUM => f(Some(self.alarm)),
            capsules::adc::DRIVER_NUM => f(Some(self.adc)),
            capsules::led::DRIVER_NUM => f(Some(self.led)),
            capsules::i2c_master_slave_driver::DRIVER_NUM => f(Some(self.i2c_master_slave)),
            capsules::rng::DRIVER_NUM => f(Some(self.rng)),
            capsules::app_flash_driver::DRIVER_NUM => f(Some(self.app_flash)),

            signpost_drivers::app_watchdog::DRIVER_NUM => f(Some(self.app_watchdog)),

            signpost_drivers::signpost_tock_firmware_update::DRIVER_NUM => f(Some(self.stfu)),
            signpost_drivers::signpost_tock_firmware_update::DRIVER_NUM_HOLDING => f(Some(self.stfu_holding)),

            kernel::ipc::DRIVER_NUM => f(Some(&self.ipc)),
            _ => f(None)
        }
    }
}


unsafe fn set_pin_primary_functions() {
    use sam4l::gpio::{PA, PB};
    use sam4l::gpio::PeripheralFunction::{A, B};

	// Analog inputs
    PB[02].configure(Some(A)); // MEMS microphone
    PB[03].configure(Some(A)); // External microphone

    // MSGEQ control signals
    PA[07].configure(None);    // spec 2 reset
    PA[08].configure(None);    // spec 2 power
    PA[06].configure(None);    // spec 2 strobe
    PA[05].configure(Some(A));    // spec 2 out
    PA[10].configure(None);    // spec strobe
    PB[00].configure(None);    // spec reset
    PB[01].configure(None);    // spec power
    PA[04].configure(Some(A));    // spec out

    // LEDs
    PB[04].configure(None);    // LEDG2 (LED3)
    PB[05].configure(None);    // LEDR2 (LED4)
    PB[06].configure(None);    // LEDG1 (LED1)
    PB[07].configure(None);    // LEDR1 (LED2)

    // Flash chip
    PA[15].configure(None);    // !FLASH_CS
    PB[11].configure(None);    // !FLASH_RESET
    // using USART3 on 64 pin SAM4L
    PB[08].configure(Some(A)); // FLASH_SCLK
    PB[09].configure(Some(A)); // FLASH_SI
    PB[10].configure(Some(A)); // FLASH_SO

    // Debug lines
    PA[18].configure(None);    // PPS
    // using USART2 on 64 pin SAM4L
    PA[19].configure(None); // Mod out
    PA[20].configure(None); // Mod in
    // using USART0 on 64 pin SAM4L
    PA[12].configure(Some(A)); // DBG_TX
    PA[11].configure(Some(A)); // DBG_RX
    // using TWIMS0 (I2C)
    PA[23].configure(Some(B)); // SDA
	PA[24].configure(Some(B)); // SCL
    // using USBC
	PA[25].configure(Some(A)); // USB-
	PA[26].configure(Some(A)); // USB+
	PB[14].configure(None);    // DBG_GPIO1
	PB[15].configure(None);    // DBG_GPIO2

}

/*******************************************************************************
 * Main init function
 ******************************************************************************/

#[no_mangle]
pub unsafe fn reset_handler() {
    sam4l::init();

    sam4l::pm::PM.setup_system_clock(sam4l::pm::SystemClockSource::PllExternalOscillatorAt48MHz {
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
        capsules::console::Console<usart::USART>,
        capsules::console::Console::new(&usart::USART0,
                     115200,
                     &mut capsules::console::WRITE_BUF,
                     kernel::Grant::create()));
    hil::uart::UART::set_client(&usart::USART0, console);

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
    let alarm = static_init!(
        capsules::alarm::AlarmDriver<'static, VirtualMuxAlarm<'static, sam4l::ast::Ast>>,
        capsules::alarm::AlarmDriver::new(virtual_alarm1, kernel::Grant::create()));
    virtual_alarm1.set_client(alarm);

    //
    // I2C Buses
    //
    let i2c_modules = static_init!(
        capsules::i2c_master_slave_driver::I2CMasterSlaveDriver<'static>,
        capsules::i2c_master_slave_driver::I2CMasterSlaveDriver::new(&sam4l::i2c::I2C0,
            &mut capsules::i2c_master_slave_driver::BUFFER1,
            &mut capsules::i2c_master_slave_driver::BUFFER2,
            &mut capsules::i2c_master_slave_driver::BUFFER3));
    sam4l::i2c::I2C0.set_master_client(i2c_modules);
    sam4l::i2c::I2C0.set_slave_client(i2c_modules);

    //
    //ADC
    //
    let adc_channels = static_init!(
        [&'static sam4l::adc::AdcChannel; 6],
        [&sam4l::adc::CHANNEL_AD0, // A0
         &sam4l::adc::CHANNEL_AD1, // A1
         &sam4l::adc::CHANNEL_AD3, // A2
         &sam4l::adc::CHANNEL_AD4, // A3
         &sam4l::adc::CHANNEL_AD5, // A4
         &sam4l::adc::CHANNEL_AD6, // A5
        ]
    );
    let adc = static_init!(
        capsules::adc::Adc<'static, sam4l::adc::Adc>,
        capsules::adc::Adc::new(&mut sam4l::adc::ADC0, adc_channels,
                                &mut capsules::adc::ADC_BUFFER1,
                                &mut capsules::adc::ADC_BUFFER2,
                                &mut capsules::adc::ADC_BUFFER3));
    sam4l::adc::ADC0.set_client(adc);

    // Setup RNG
    let rng = static_init!(
            capsules::rng::SimpleRng<'static, sam4l::trng::Trng>,
            capsules::rng::SimpleRng::new(&sam4l::trng::TRNG, kernel::Grant::create()));
    sam4l::trng::TRNG.set_client(rng);

    // Nonvolatile Pages
    pub static mut PAGEBUFFER: sam4l::flashcalw::Sam4lPage = sam4l::flashcalw::Sam4lPage::new();
    let nv_to_page = static_init!(
        capsules::nonvolatile_to_pages::NonvolatileToPages<'static, sam4l::flashcalw::FLASHCALW>,
        capsules::nonvolatile_to_pages::NonvolatileToPages::new(
            &mut sam4l::flashcalw::FLASH_CONTROLLER,
            &mut PAGEBUFFER));
    hil::flash::HasClient::set_client(&sam4l::flashcalw::FLASH_CONTROLLER, nv_to_page);

    // App Flash
    pub static mut APP_FLASH_BUFFER: [u8; 512] = [0; 512];
    let app_flash = static_init!(
        capsules::app_flash_driver::AppFlash<'static>,
        capsules::app_flash_driver::AppFlash::new(nv_to_page,
            kernel::Grant::create(), &mut APP_FLASH_BUFFER));
    hil::nonvolatile_storage::NonvolatileStorage::set_client(nv_to_page, app_flash);
    sam4l::flashcalw::FLASH_CONTROLLER.configure();

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
        signpost_drivers::app_watchdog::Timeout::new(app_timeout_alarm, signpost_drivers::app_watchdog::TimeoutMode::App, 1000, cortexm4::scb::reset),
        128/8);
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
    // Remaining GPIO pins
    //
    let gpio_pins = static_init!(
        [&'static sam4l::gpio::GPIOPin; 11],
        [&sam4l::gpio::PA[19], //Mod out
         &sam4l::gpio::PA[20], //Mod in
         &sam4l::gpio::PA[18], //PPS
         &sam4l::gpio::PA[10], // spec strobe
         &sam4l::gpio::PB[00], // spec reset
         &sam4l::gpio::PB[01], // spec power
         //&sam4l::gpio::PA[04], // spec out

         &sam4l::gpio::PA[06], // spec 2 strobe
         &sam4l::gpio::PA[07], // spec 2 reset
         &sam4l::gpio::PA[08], // spec 2 power
         //&sam4l::gpio::PA[05], // spec 2 out

         &sam4l::gpio::PA[15], // !FLASH_CS
         &sam4l::gpio::PB[11]] // !FLASH_RESET
    );
    let gpio = static_init!(
        capsules::gpio::GPIO<'static, sam4l::gpio::GPIOPin>,
        capsules::gpio::GPIO::new(gpio_pins));
    for pin in gpio_pins.iter() {
        pin.set_client(gpio);
    }

    //
    // LEDs
    //
    let led_pins = static_init!(
        [(&'static sam4l::gpio::GPIOPin, capsules::led::ActivationMode); 6],
          [(&sam4l::gpio::PB[15], capsules::led::ActivationMode::ActiveHigh), //DBG_GPIO1
           (&sam4l::gpio::PB[14], capsules::led::ActivationMode::ActiveHigh), //DBG_GPIO2
           (&sam4l::gpio::PB[06], capsules::led::ActivationMode::ActiveLow),  // LEDG1
           (&sam4l::gpio::PB[07], capsules::led::ActivationMode::ActiveLow),  // LEDR1
           (&sam4l::gpio::PB[04], capsules::led::ActivationMode::ActiveLow),  // LEDG2
           (&sam4l::gpio::PB[05], capsules::led::ActivationMode::ActiveLow)]  // LEDR2
           );
    let led = static_init!(
        capsules::led::LED<'static, sam4l::gpio::GPIOPin>,
        capsules::led::LED::new(led_pins));

    // configure initial state for debug LEDs
    sam4l::gpio::PB[15].clear(); // red LED off
    sam4l::gpio::PB[14].set();   // green LED on

    //
    // Flash
    //

    let mux_flash = static_init!(
        capsules::virtual_flash::MuxFlash<'static, sam4l::flashcalw::FLASHCALW>,
        capsules::virtual_flash::MuxFlash::new(&sam4l::flashcalw::FLASH_CONTROLLER));
    hil::flash::HasClient::set_client(&sam4l::flashcalw::FLASH_CONTROLLER, mux_flash);

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
    //
    // Actual platform object
    //
    let audio_module = AudioModule {
        console: console,
        gpio: gpio,
        led: led,
        alarm: alarm,
        i2c_master_slave: i2c_modules,
        adc: adc,
        app_watchdog: app_watchdog,
        rng: rng,
        app_flash: app_flash,
        stfu: stfu,
        stfu_holding: stfu_holding,
        ipc: kernel::ipc::IPC::new(),
    };

    audio_module.console.initialize();
    // Attach the kernel debug interface to this console
    let kc = static_init!(
        capsules::console::App,
        capsules::console::App::default());
    kernel::debug::assign_console_driver(Some(audio_module.console), kc);

    //watchdog.start();

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

    kernel::main(&audio_module, &mut chip, &mut PROCESSES, &audio_module.ipc);
}
