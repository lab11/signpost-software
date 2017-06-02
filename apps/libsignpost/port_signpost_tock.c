#include <stdint.h>
#include <string.h>
#include "tock.h"
#include "port_signpost.h"
#include "i2c_master_slave.h"
#include "gpio.h"
#include "led.h"
#include "timer.h"

/* This is a port interface for the signpost API and networking stack!
The goal is to export the minimal interface such that platforms
can integrate with the signpost.

From a high level you will need to provide:
    - Blocking I2C write function (currently at least 255 bytes at a time)
    - Asynchronous I2C slave function (listen on an address and send
                                        back written bytes asynchronously)
    - Set and clear of the MOD_OUT pin
    - Asynchronous callback for falling edge of the Mod-in pin
    - Currently the signpost libraries assume printf functionality
        - This is assumed to be provided through <stdio.h>

To provide this functionality please implement the following interface
for each platform*/

static bool master_write_yield_flag = false;
static int  master_write_len_or_rc = 0;

port_signpost_callback slave_write_cb;
static void i2c_master_slave_callback(
        int callback_type,
        int length,
        __attribute__ ((unused)) int unused,
        __attribute__ ((unused)) void* callback_args) {
    if(callback_type == TOCK_I2C_CB_SLAVE_WRITE) {
        slave_write_cb(length);
    }
    else if(callback_type == TOCK_I2C_CB_MASTER_WRITE) {
        master_write_yield_flag = true;
        master_write_len_or_rc = length;
    }
}

// Should be a linked list of callbacks and pins, but for now just keep track
// of a single interrupt callback
port_signpost_callback gpio_interrupt_cb;
static void port_signpost_gpio_interrupt_callback(
        __attribute__ ((unused)) int pin,
        __attribute__ ((unused)) int state,
        __attribute__ ((unused)) int unused,
        __attribute__ ((unused)) void* callback_args) {
    gpio_interrupt_cb(0);
}

static uint8_t master_write_buf[I2C_MAX_LEN];
static uint8_t slave_read_buf[I2C_MAX_LEN];

//This function is called upon signpost initialization
//You should use it to set up the i2c interface
int port_signpost_init(uint8_t i2c_address) {
    int rc = 0;
    rc = i2c_master_slave_set_master_write_buffer(master_write_buf, I2C_MAX_LEN);
    if (rc < 0) return rc;
    rc = i2c_master_slave_set_slave_read_buffer(slave_read_buf, I2C_MAX_LEN);
    if (rc < 0) return rc;
    rc = i2c_master_slave_set_slave_address(i2c_address);
    if (rc < 0) return rc;
    return i2c_master_slave_set_callback(i2c_master_slave_callback, NULL);
}

//This function is a blocking i2c send call
//it should return the length of the message successfully sent on the bus
//If the bus returns an error, use the appropriate error code
//defined in this file
int port_signpost_i2c_master_write(uint8_t dest, uint8_t* buf, size_t len) {
    int rc = 0;
    memcpy(master_write_buf, buf, len);
    master_write_yield_flag = false;
    rc = i2c_master_slave_write(dest, len);
    if (rc < 0) return rc;

    yield_for(&master_write_yield_flag);
    return master_write_len_or_rc;
}

//This function sets up the asynchronous i2c receive interface
//When this function is called start listening on the i2c bus for
//The address specified in init
//Place data in the buffer no longer than the max len
int port_signpost_i2c_slave_listen(port_signpost_callback cb, uint8_t* buf, size_t max_len) {
    int rc = 0;
    rc = i2c_master_slave_set_slave_write_buffer(buf, max_len);
    if (rc < 0) return rc;
    slave_write_cb = cb;
    return i2c_master_slave_listen();
}

int port_signpost_i2c_slave_read_setup(uint8_t *buf, size_t len) {
    memcpy(slave_read_buf, buf, len);
    return i2c_master_slave_enable_slave_read(len);
}

//These functions are used to control gpio outputs
int port_signpost_gpio_enable_output(int pin) {
    return gpio_enable_output(pin);
}

int port_signpost_gpio_set(int pin) {
    return gpio_set(pin);
}

int port_signpost_gpio_clear(int pin) {
     return gpio_clear(pin);
}

int port_signpost_gpio_read(int pin) {
     return gpio_read(pin);
}

//This function is used to get the input interrupt for the falling edge of
//mod-in
int port_signpost_gpio_enable_interrupt(int pin, InputMode input_mode, InterruptMode interrupt_mode, port_signpost_callback cb) {
    int rc = 0;
    gpio_interrupt_cb = cb;
    rc = gpio_interrupt_callback(port_signpost_gpio_interrupt_callback, NULL);
    if (rc < 0) return rc;
    return gpio_enable_interrupt(pin, (GPIO_InputMode_t) input_mode, (GPIO_InterruptMode_t) interrupt_mode);
}

int port_signpost_gpio_disable_interrupt(int pin) {
    return gpio_disable_interrupt(pin);
}

void port_signpost_wait_for(void* wait_on_true){
    yield_for(wait_on_true);
}

void port_signpost_delay_ms(int ms) {
    delay_ms(ms);
}
