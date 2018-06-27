#![no_std]
#![no_main]
#![feature(panic_implementation)]
#![feature(const_fn,lang_items)]

extern crate capsules;
extern crate cortexm4;
#[macro_use(debug,static_init)]
extern crate kernel;
extern crate sam4l;

extern crate signpost_drivers;
extern crate signpost_hil;

use signpost_drivers::gps_console;
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
const FAULT_RESPONSE: kernel::procs::FaultResponse = kernel::procs::FaultResponse::Panic;

#[link_section = ".app_memory"]
static mut APP_MEMORY: [u8; 16384*2] = [0; 16384*2];

// Actual memory for holding the active process structures.
static mut PROCESSES: [Option<&'static mut kernel::procs::Process<'static>>; NUM_PROCS] = [None, None];

/*******************************************************************************
 * Setup this platform
 ******************************************************************************/

struct DebugRadioModule {
    gps_console: &'static signpost_drivers::gps_console::Console<'static, usart::USART>,
    gpio: &'static capsules::gpio::GPIO<'static, sam4l::gpio::GPIOPin>,
    timer: &'static capsules::alarm::AlarmDriver<'static, VirtualMuxAlarm<'static, sam4l::ast::Ast<'static>>>,
    i2c_master_slave: &'static capsules::i2c_master_slave_driver::I2CMasterSlaveDriver<'static>,
    app_watchdog: &'static signpost_drivers::app_watchdog::AppWatchdog<'static, VirtualMuxAlarm<'static, sam4l::ast::Ast<'static>>>,
    rng: &'static capsules::rng::SimpleRng<'static, sam4l::trng::Trng<'static>>,
    app_flash: &'static capsules::app_flash_driver::AppFlash<'static>,
    ipc: kernel::ipc::IPC,
}

impl Platform for DebugRadioModule {
    fn with_driver<F, R>(&self, driver_num: usize, f: F) -> R
        where F: FnOnce(Option<&kernel::Driver>) -> R
    {

        match driver_num {
            capsules::console::DRIVER_NUM => f(Some(self.gps_console)),
            capsules::gpio::DRIVER_NUM => f(Some(self.gpio)),
            capsules::alarm::DRIVER_NUM => f(Some(self.timer)),
            13 => f(Some(self.i2c_master_slave)),
            capsules::rng::DRIVER_NUM => f(Some(self.rng)),
            capsules::app_flash_driver::DRIVER_NUM => f(Some(self.app_flash)),

            signpost_drivers::app_watchdog::DRIVER_NUM => f(Some(self.app_watchdog)),
            signpost_drivers::gps_console::DRIVER_NUM => f(Some(self.gps_console)),

            kernel::ipc::DRIVER_NUM => f(Some(&self.ipc)),
            _ => f(None)
        }
    }
}

unsafe fn set_pin_primary_functions() {
    use sam4l::gpio::{PA};
    use sam4l::gpio::PeripheralFunction::{A, B};

    PA[11].configure(Some(A)); // Radio RX
    PA[12].configure(Some(A)); // Radio TX
    PA[18].configure(None);    // PPS
    PA[19].configure(None);    // MOD_OUT
    PA[20].configure(None);    // MOD_IN
    PA[23].configure(Some(B)); // SDA
    PA[24].configure(Some(B)); // SCL
    PA[25].configure(Some(A)); // USB
    PA[26].configure(Some(A)); // USB
    PA[05].configure(None);    // USB
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

    //as a hack we are going to use gps console because it can receive bytes
    let gps_console = static_init!(
        signpost_drivers::gps_console::Console<usart::USART>,
        signpost_drivers::gps_console::Console::new(&usart::USART0,
                     115200,
                     &mut gps_console::WRITE_BUF,
                     &mut gps_console::READ_BUF,
                     kernel::Grant::create()));
    hil::uart::UART::set_client(&usart::USART0, gps_console);

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

    hil::i2c::I2CSlave::set_address(&sam4l::i2c::I2C0, 0x22);

    //
    // Remaining GPIO pins
    //
    let gpio_pins = static_init!(
        [&'static sam4l::gpio::GPIOPin; 4],
        [&sam4l::gpio::PA[19], // MOD_OUT
         &sam4l::gpio::PA[20], // MOD_IN
         &sam4l::gpio::PA[18], // PPS
         &sam4l::gpio::PA[05]]
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
        signpost_drivers::app_watchdog::Timeout::new(kernel_timeout_alarm, signpost_drivers::app_watchdog::TimeoutMode::Kernel, 500, cortexm4::scb::reset));
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
    let module = DebugRadioModule {
        gps_console: gps_console,
        gpio: gpio,
        timer: timer,
        i2c_master_slave: i2c_modules,
        app_watchdog: app_watchdog,
        rng: rng,
        app_flash: app_flash,
        ipc: kernel::ipc::IPC::new(),
    };

    module.gps_console.initialize();
    // Attach the kernel debug interface to this console
    let kc = static_init!(
        signpost_drivers::gps_console::App,
        signpost_drivers::gps_console::App::default());
    kernel::debug::assign_console_driver(Some(module.gps_console), kc);
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
    kernel::procs::load_processes(&_sapps as *const u8,
                                    &mut APP_MEMORY,
                                    &mut PROCESSES,
                                    FAULT_RESPONSE);

    kernel::kernel_loop(&module, &mut chip, &mut PROCESSES, Some(&module.ipc));
}
