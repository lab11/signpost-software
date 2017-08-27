#include <stdint.h>
#include <string.h>
#include <stdarg.h>

#include "gpio.h"
#include "console.h"
#include "i2c_master_slave.h"
#include "led.h"
#include "port_signpost.h"
#include "rng.h"
#include "signpost_entropy.h"
#include "timer.h"
#include "tock.h"

#define MOD_OUT 0
#define MOD_IN  1
#define PPS     2

#define DEBUG_LED 0 // DEBUG_GPIO1

//All implementations must implement a port_print_buf for signbus layer printing
char port_print_buf[80];

static bool master_write_yield_flag = false;
static int  master_write_len_or_rc = 0;

static port_signpost_callback global_slave_write_cb;
static void i2c_master_slave_callback(
        int callback_type,
        int length,
        __attribute__ ((unused)) int unused,
        __attribute__ ((unused)) void* callback_args) {
    if(callback_type == TOCK_I2C_CB_SLAVE_WRITE) {
        global_slave_write_cb(length);
    }
    else if(callback_type == TOCK_I2C_CB_MASTER_WRITE) {
        master_write_yield_flag = true;
        master_write_len_or_rc = length;
    }
}

// Should be a linked list of callbacks and pins, but for now just keep track
// of a single interrupt callback
static port_signpost_callback global_gpio_interrupt_cb;
static void port_signpost_gpio_interrupt_callback(
        __attribute__ ((unused)) int pin,
        __attribute__ ((unused)) int state,
        __attribute__ ((unused)) int unused,
        __attribute__ ((unused)) void* callback_args) {
    global_gpio_interrupt_cb(TOCK_SUCCESS);
}

static uint8_t master_write_buf[I2C_MAX_LEN];
static uint8_t slave_read_buf[I2C_MAX_LEN];

//This function is called upon signpost initialization
//You should use it to set up the i2c interface
int port_signpost_init(uint8_t i2c_address) {
    int rc;
    rc = i2c_master_slave_set_master_write_buffer(master_write_buf, I2C_MAX_LEN);
    if (rc < 0) return SB_PORT_FAIL;
    rc = i2c_master_slave_set_slave_read_buffer(slave_read_buf, I2C_MAX_LEN);
    if (rc < 0) return SB_PORT_FAIL;
    rc = i2c_master_slave_set_slave_address(i2c_address);
    if (rc < 0) return SB_PORT_FAIL;
    rc = i2c_master_slave_set_callback(i2c_master_slave_callback, NULL);
    if (rc < 0) return SB_PORT_FAIL;
    return SB_PORT_SUCCESS;
}

//This function is a blocking i2c send call
//it should return the length of the message successfully sent on the bus
//If the bus returns an error, use the appropriate error code
//defined in this file
int port_signpost_i2c_master_write(uint8_t dest, uint8_t* buf, size_t len) {
    int rc;
    memcpy(master_write_buf, buf, len);
    master_write_yield_flag = false;
    rc = i2c_master_slave_write(dest, len);
    if (rc < 0) return SB_PORT_FAIL;

    yield_for(&master_write_yield_flag);
    if (master_write_len_or_rc < 0) return SB_PORT_FAIL;
    return master_write_len_or_rc;
}

//This function sets up the asynchronous i2c receive interface
//When this function is called start listening on the i2c bus for
//The address specified in init
//Place data in the buffer no longer than the max len
int port_signpost_i2c_slave_listen(port_signpost_callback cb, uint8_t* buf, size_t max_len) {
    int rc;
    rc = i2c_master_slave_set_slave_write_buffer(buf, max_len);
    if (rc < 0) return SB_PORT_FAIL;
    global_slave_write_cb = cb;
    rc = i2c_master_slave_listen();
    if (rc < 0) return SB_PORT_FAIL;
    return SB_PORT_SUCCESS;
}

int port_signpost_i2c_slave_read_setup(uint8_t *buf, size_t len) {
    int rc;
    memcpy(slave_read_buf, buf, len);
    return i2c_master_slave_enable_slave_read(len);
    if (rc < 0) return SB_PORT_FAIL;
    return SB_PORT_SUCCESS;
}

//These functions are used to control gpio outputs
int port_signpost_mod_out_set(void) {
    int rc;
    gpio_enable_output(MOD_OUT);
    rc = gpio_set(MOD_OUT);
    if (rc < 0) return SB_PORT_FAIL;
    return SB_PORT_SUCCESS;
}

int port_signpost_mod_out_clear(void) {
    int rc;
    gpio_enable_output(MOD_OUT);
    rc = gpio_clear(MOD_OUT);
    if (rc < 0) return SB_PORT_FAIL;
    return SB_PORT_SUCCESS;
}

int port_signpost_mod_in_read(void) {
    gpio_enable_input(MOD_IN, PullNone);
    return gpio_read(MOD_IN);
}

int port_signpost_pps_read(void) {
    gpio_enable_input(PPS, PullNone);
    return gpio_read(PPS);
}

//This function is used to get the input interrupt for the falling edge of
//mod-in
int port_signpost_mod_in_enable_interrupt_falling(port_signpost_callback cb) {
    int rc;
    global_gpio_interrupt_cb = cb;
    rc = gpio_interrupt_callback(port_signpost_gpio_interrupt_callback, NULL);
    if (rc < 0) return SB_PORT_FAIL;
    rc = gpio_enable_interrupt(MOD_IN, PullUp, FallingEdge);
    if (rc < 0) return SB_PORT_FAIL;
    return SB_PORT_SUCCESS;
}

//This function is used to get the input interrupt for the rising edge of
//mod-in
int port_signpost_mod_in_enable_interrupt_rising(port_signpost_callback cb) {
    int rc;
    global_gpio_interrupt_cb = cb;
    rc = gpio_interrupt_callback(port_signpost_gpio_interrupt_callback, NULL);
    if (rc < 0) return SB_PORT_FAIL;
    rc = gpio_enable_interrupt(MOD_IN, PullUp, RisingEdge);
    if (rc < 0) return SB_PORT_FAIL;
    return SB_PORT_SUCCESS;
}

int port_signpost_mod_in_disable_interrupt(void) {
    int rc;
    rc = gpio_disable_interrupt(MOD_IN);
    if (rc < 0) return SB_PORT_FAIL;
    return SB_PORT_SUCCESS;
}

void port_signpost_wait_for(void* wait_on_true){
    yield_for(wait_on_true);
}

int port_signpost_wait_for_with_timeout(void* wait_on_true, uint32_t ms) {
    if (yield_for_with_timeout(wait_on_true, ms) == TOCK_SUCCESS) {
      return SB_PORT_SUCCESS;
    }
    return SB_PORT_FAIL;
}

void port_signpost_delay_ms(unsigned ms) {
    delay_ms(ms);
}

int port_signpost_debug_led_on(void) {
    int rc;
    rc = led_on(DEBUG_LED);
    if (rc < 0) return SB_PORT_FAIL;
    return SB_PORT_SUCCESS;
}

int port_signpost_debug_led_off(void){
    int rc;
    rc = led_off(DEBUG_LED);
    if (rc < 0) return SB_PORT_FAIL;
    return SB_PORT_SUCCESS;
}

int port_rng_init(void) {
    return SB_PORT_SUCCESS;
}

int port_rng_sync(uint8_t* buf, uint32_t len, uint32_t num) {
    int rc = rng_sync(buf, len, num);
    if (rc >= 0) return rc;
    else return SB_PORT_FAIL;
}

int port_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int rc = vprintf(fmt, args);
    va_end(args);
    return rc;
}
