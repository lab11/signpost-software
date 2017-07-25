#![crate_name = "ambient_module"]
#![no_std]
#![no_main]
#![feature(asm,compiler_builtins_lib,const_fn,drop_types_in_const,lang_items)]

extern crate capsules;
extern crate compiler_builtins;
extern crate cortexm4;
#[macro_use(debug, static_init)]
extern crate kernel;
extern crate sam4l;

extern crate signpost_drivers;
extern crate signpost_hil;

use capsules::console::{self, Console};
use capsules::timer::TimerDriver;
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

struct AmbientModule {
    console: &'static Console<'static, usart::USART>,
    gpio: &'static capsules::gpio::GPIO<'static, sam4l::gpio::GPIOPin>,
    led: &'static capsules::led::LED<'static, sam4l::gpio::GPIOPin>,
    timer: &'static TimerDriver<'static, VirtualMuxAlarm<'static, sam4l::ast::Ast<'static>>>,
    i2c_master_slave: &'static capsules::i2c_master_slave_driver::I2CMasterSlaveDriver<'static>,
    // lps331ap: &'static signpost_drivers::lps331ap::LPS331AP<'static>,
    lps25hb: &'static capsules::lps25hb::LPS25HB<'static>,
    si7021: &'static capsules::si7021::SI7021<'static, VirtualMuxAlarm<'static, sam4l::ast::Ast<'static>>>,
    isl29035: &'static capsules::isl29035::Isl29035<'static, VirtualMuxAlarm<'static, sam4l::ast::Ast<'static>>>,
    tsl2561: &'static capsules::tsl2561::TSL2561<'static>,
    app_watchdog: &'static signpost_drivers::app_watchdog::AppWatchdog<'static, VirtualMuxAlarm<'static, sam4l::ast::Ast<'static>>>,
    rng: &'static capsules::rng::SimpleRng<'static, sam4l::trng::Trng<'static>>,
    app_flash: &'static capsules::app_flash_driver::AppFlash<'static>,
    ipc: kernel::ipc::IPC,
}

impl Platform for AmbientModule {
    fn with_driver<F, R>(&self, driver_num: usize, f: F) -> R
        where F: FnOnce(Option<&kernel::Driver>) -> R
    {
        match driver_num {
            0 => f(Some(self.console)),
            1 => f(Some(self.gpio)),

            3 => f(Some(self.timer)),

            6 => f(Some(self.isl29035)),

            8 => f(Some(self.led)),

            10 => f(Some(self.si7021)),
            11 => f(Some(self.lps25hb)),
            12 => f(Some(self.tsl2561)),
            13 => f(Some(self.i2c_master_slave)),
            14 => f(Some(self.rng)),
            30 => f(Some(self.app_flash)),

            108 => f(Some(self.app_watchdog)),

            0xff => f(Some(&self.ipc)),
            _ => f(None)
        }
    }
}


unsafe fn set_pin_primary_functions() {
    use sam4l::gpio::PA;
    use sam4l::gpio::PeripheralFunction::{A, B, E};

    PA[04].configure(None); // PIR
    PA[05].configure(None); // LED1
    PA[06].configure(None); // LED2
    PA[07].configure(None); // LED3
    PA[08].configure(None); // LPS35 Pressure Sensor Interrupt
    PA[09].configure(None); // Unused
    PA[10].configure(None); // LPS25HB Pressure Sensor Interrupt
    PA[11].configure(Some(A)); // UART RX
    PA[12].configure(Some(A)); // UART TX
    PA[13].configure(None); // Unused
    PA[14].configure(None); // LPS331AP Pressure Sensor Interrupt 1
    PA[15].configure(None); // LPS331AP Pressure Sensor Interrupt 2
    PA[16].configure(None); // TSL2561 Light Sensor Interrupt
    PA[17].configure(None); // ISL29035 Light Sensor Interrupt
    PA[18].configure(None); // Module Out
    PA[19].configure(None); // PPS
    PA[20].configure(None); // Module In
    PA[21].configure(Some(E)); // Sensor I2C SDA
    PA[22].configure(Some(E)); // Sensor I2C SCL
    PA[23].configure(Some(B)); // Backplane I2C SDA
    PA[24].configure(Some(B)); // Backplane I2C SCL
    PA[25].configure(Some(A)); // USB-
    PA[26].configure(Some(A)); // USB+

    // Setup unused pins as inputs
    sam4l::gpio::PA[09].enable();
    sam4l::gpio::PA[09].disable_output();
    sam4l::gpio::PA[13].enable();
    sam4l::gpio::PA[13].disable_output();
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
        Console<usart::USART>,
        Console::new(&usart::USART0,
                     115200,
                     &mut console::WRITE_BUF,
                     kernel::Container::create()));
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
    let timer = static_init!(
        TimerDriver<'static, VirtualMuxAlarm<'static, sam4l::ast::Ast>>,
        TimerDriver::new(virtual_alarm1, kernel::Container::create()));
    virtual_alarm1.set_client(timer);

    // Setup RNG
    let rng = static_init!(
            capsules::rng::SimpleRng<'static, sam4l::trng::Trng>,
            capsules::rng::SimpleRng::new(&sam4l::trng::TRNG, kernel::Container::create()));
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
            kernel::Container::create(), &mut APP_FLASH_BUFFER));

    //
    // I2C Buses
    //
    // To Backplane
    let i2c_master_slave = static_init!(
        capsules::i2c_master_slave_driver::I2CMasterSlaveDriver<'static>,
        capsules::i2c_master_slave_driver::I2CMasterSlaveDriver::new(&sam4l::i2c::I2C0,
            &mut capsules::i2c_master_slave_driver::BUFFER1,
            &mut capsules::i2c_master_slave_driver::BUFFER2,
            &mut capsules::i2c_master_slave_driver::BUFFER3));
    sam4l::i2c::I2C0.set_master_client(i2c_master_slave);
    sam4l::i2c::I2C0.set_slave_client(i2c_master_slave);

    // Set I2C slave address here, because it is board specific and not app
    // specific. It can be overridden in the app, of course.
    hil::i2c::I2CSlave::set_address(&sam4l::i2c::I2C0, 0x32);

    // Sensors
    let i2c_mux_sensors = static_init!(
        capsules::virtual_i2c::MuxI2C<'static>,
        capsules::virtual_i2c::MuxI2C::new(&sam4l::i2c::I2C2));
    sam4l::i2c::I2C2.set_master_client(i2c_mux_sensors);

    //
    // Sensors
    //

    // SI7021 Temperature / Humidity
    let si7021_i2c = static_init!(
        capsules::virtual_i2c::I2CDevice,
        capsules::virtual_i2c::I2CDevice::new(i2c_mux_sensors, 0x40));
    let si7021_virtual_alarm = static_init!(
        VirtualMuxAlarm<'static, sam4l::ast::Ast>,
        VirtualMuxAlarm::new(mux_alarm));
    let si7021 = static_init!(
        capsules::si7021::SI7021<'static, VirtualMuxAlarm<'static, sam4l::ast::Ast>>,
        capsules::si7021::SI7021::new(si7021_i2c,
            si7021_virtual_alarm,
            &mut capsules::si7021::BUFFER));
    si7021_i2c.set_client(si7021);
    si7021_virtual_alarm.set_client(si7021);

    // // LPS331AP Pressure Sensor
    // let lps331ap_i2c = static_init!(
    //     capsules::virtual_i2c::I2CDevice,
    //     capsules::virtual_i2c::I2CDevice::new(i2c_mux_sensors, 0x5C));
    // let lps331ap = static_init!(
    //     signpost_drivers::lps331ap::LPS331AP<'static>,
    //     signpost_drivers::lps331ap::LPS331AP::new(lps331ap_i2c,
    //         &sam4l::gpio::PA[14],
    //         &mut signpost_drivers::lps331ap::BUFFER));
    // lps331ap_i2c.set_client(lps331ap);
    // sam4l::gpio::PA[14].set_client(lps331ap);

    // LPS25HB Pressure Sensor
    let lps25hb_i2c = static_init!(
        capsules::virtual_i2c::I2CDevice,
        capsules::virtual_i2c::I2CDevice::new(i2c_mux_sensors, 0x5C));
    let lps25hb = static_init!(
        capsules::lps25hb::LPS25HB<'static>,
        capsules::lps25hb::LPS25HB::new(lps25hb_i2c,
            &sam4l::gpio::PA[10],
            &mut capsules::lps25hb::BUFFER));
    lps25hb_i2c.set_client(lps25hb);
    sam4l::gpio::PA[10].set_client(lps25hb);

    // TSL2561 Light Sensor
    let tsl2561_i2c = static_init!(
        capsules::virtual_i2c::I2CDevice,
        capsules::virtual_i2c::I2CDevice::new(i2c_mux_sensors, 0x29));
    let tsl2561 = static_init!(
        capsules::tsl2561::TSL2561<'static>,
        capsules::tsl2561::TSL2561::new(tsl2561_i2c,
            &sam4l::gpio::PA[16],
            &mut capsules::tsl2561::BUFFER));
    tsl2561_i2c.set_client(tsl2561);
    sam4l::gpio::PA[16].set_client(tsl2561);

    // Configure the ISL29035, device address 0x44
    let isl29035_i2c = static_init!(
        capsules::virtual_i2c::I2CDevice,
        capsules::virtual_i2c::I2CDevice::new(i2c_mux_sensors, 0x44));
    let isl29035_virtual_alarm = static_init!(
        VirtualMuxAlarm<'static, sam4l::ast::Ast>,
        VirtualMuxAlarm::new(mux_alarm));
    let isl29035 = static_init!(
        capsules::isl29035::Isl29035<'static, VirtualMuxAlarm<'static, sam4l::ast::Ast<'static>>>,
        capsules::isl29035::Isl29035::new(isl29035_i2c, isl29035_virtual_alarm, &mut capsules::isl29035::BUF));
    isl29035_i2c.set_client(isl29035);
    isl29035_virtual_alarm.set_client(isl29035);

    //
    // LEDs
    //
    let led_pins = static_init!(
        [(&'static sam4l::gpio::GPIOPin, capsules::led::ActivationMode); 3],
        [(&sam4l::gpio::PA[06], capsules::led::ActivationMode::ActiveHigh), // LED2, Debug GPIO1
         (&sam4l::gpio::PA[07], capsules::led::ActivationMode::ActiveHigh), // LED3, Debug GPIO2
         (&sam4l::gpio::PA[05], capsules::led::ActivationMode::ActiveLow),  // LED1
        ]);
    let led = static_init!(
        capsules::led::LED<'static, sam4l::gpio::GPIOPin>,
        capsules::led::LED::new(led_pins));

    // configure initial state for debug LEDs
    sam4l::gpio::PA[06].clear(); // red LED off
    sam4l::gpio::PA[07].set();   // green LED on

    //
    // Remaining GPIO pins
    //
    let gpio_pins = static_init!(
        [&'static sam4l::gpio::GPIOPin; 3],
        [&sam4l::gpio::PA[18], //Mod out
         &sam4l::gpio::PA[20], //Mod in
         &sam4l::gpio::PA[19]] //PPS
    );
    let gpio = static_init!(
        capsules::gpio::GPIO<'static, sam4l::gpio::GPIOPin>,
        capsules::gpio::GPIO::new(gpio_pins));
    for pin in gpio_pins.iter() {
        pin.set_client(gpio);
    }

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
    // Actual platform object
    //
    let ambient_module =  AmbientModule {
        console: console,
        gpio: gpio,
        led: led,
        timer: timer,
        i2c_master_slave: i2c_master_slave,
        lps25hb: lps25hb,
        si7021: si7021,
        isl29035: isl29035,
        tsl2561: tsl2561,
        app_watchdog: app_watchdog,
        rng: rng,
        app_flash: app_flash,
        ipc: kernel::ipc::IPC::new(),
    };

    ambient_module.console.initialize();
    // Attach the kernel debug interface to this console
    let kc = static_init!(
        capsules::console::App,
        capsules::console::App::default());
    kernel::debug::assign_console_driver(Some(ambient_module.console), kc);

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

    kernel::main(&ambient_module, &mut chip, &mut PROCESSES, &ambient_module.ipc);
}
