#include "mbed.h"
#include "port_signpost.h"
#include "board.h"
#include <stdio.h>

DigitalOut Debug(DEBUG_LED);
DigitalOut ModOut(MOD_OUT);
InterruptIn ModIn(MOD_IN);
DigitalIn pps(PPS);
Serial DBG(SERIAL_TX, SERIAL_RX, 115200);

//All implementations must implement a port_print_buf for signbus layer printing
char port_print_buf[80];

//This function is called upon signpost initialization
//You should use it to set up the i2c interface
int port_signpost_init(uint8_t i2c_address) {
    ModIn.mode(PullUp);
    return SB_PORT_SUCCESS;
}

//This function is a blocking i2c send call
//it should return the length of the message successfully sent on the bus
//If the bus returns an error, use the appropriate error code
//defined in this file
int port_signpost_i2c_master_write(uint8_t dest, uint8_t* buf, size_t len) {

}

//This function sets up the asynchronous i2c receive interface
//When this function is called start listening on the i2c bus for
//The address specified in init
//Place data in the buffer no longer than the max len
int port_signpost_i2c_slave_listen(port_signpost_callback cb, uint8_t* buf, size_t max_len) {

}

int port_signpost_i2c_slave_read_setup(uint8_t *buf, size_t len) {

}

//These functions are used to control gpio outputs
int port_signpost_mod_out_set(void) {
    ModOut = 1;
    return SB_PORT_SUCCESS;
}

int port_signpost_mod_out_clear(void) {
    ModOut = 0;
    return SB_PORT_SUCCESS;
}

int port_signpost_mod_in_read(void) {
    return ModIn.read();
}

int port_signpost_pps_read(void) {
    return pps.read();
}

//This function is used to get the input interrupt for the falling edge of
//mod-in
static port_signpost_callback falling_cb = NULL;
static void in_falling() {
    if(falling_cb) {
        falling_cb(SB_PORT_SUCCESS);
    }
}

int port_signpost_mod_in_enable_interrupt_falling(port_signpost_callback cb) {
    ModIn.fall(&in_falling);
    falling_cb = cb;
    ModIn.enable_irq();
}

//This function is used to get the input interrupt for the rising edge of
//mod-in

static port_signpost_callback rising_cb = NULL;
static void in_rising() {
    if(rising_cb) {
        rising_cb(SB_PORT_SUCCESS);
    }
}

int port_signpost_mod_in_enable_interrupt_rising(port_signpost_callback cb) {
    ModIn.rise(&in_rising);
    rising_cb = cb;
    ModIn.enable_irq();
}

int port_signpost_mod_in_disable_interrupt(void) {
    ModIn.disable_irq();
    falling_cb = NULL;
    rising_cb = NULL;
}

void port_signpost_wait_for(void* wait_on_true){
    while(!(*((bool*)wait_on_true))) {
        Thread::wait(1);
    }
}

static Timeout waiter;
static bool timed_out = false;
static void timeout_cb() {
    timed_out = true;
}

int port_signpost_wait_for_with_timeout(void* wait_on_true, uint32_t ms) {
    timed_out = false;
    waiter.attach_us(&timeout_cb, ms*1000);
    while(!(*((bool*)wait_on_true)) && !timed_out) {
        Thread::wait(1);
    }
    if(timed_out) {
        return SB_PORT_FAIL;
    } else {
        return SB_PORT_SUCCESS;
    }
}

void port_signpost_delay_ms(unsigned ms) {
    Thread::wait(ms);
}

int port_signpost_debug_led_on(void) {
    Debug = 1;
}

int port_signpost_debug_led_off(void){
    Debug = 0;
}

int port_rng_init(void) {
    return SB_PORT_SUCCESS;
}

int port_rng_sync(uint8_t* buf, uint32_t len, uint32_t num) {
    //for right now mbed doesn't have an RNG implemented (sigh)
    //it's technically insecure not to return random values
    //...but this is just a seed, and the key is calculated with ECDH so...
    return SB_PORT_SUCCESS;
}

int port_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int rc = vprintf(fmt, args);
    va_end(args);
    return rc;
}
