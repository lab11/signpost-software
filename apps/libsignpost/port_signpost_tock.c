#include <stdint.h>
#include <string.h>

#include "gpio.h"
#include "i2c_master_slave.h"
#include "led.h"
#include "port_signpost.h"
#include "timer.h"
#include "tock.h"

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
    global_gpio_interrupt_cb(SUCCESS);
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
    global_slave_write_cb = cb;
    return i2c_master_slave_listen();
}

int port_signpost_i2c_slave_read_setup(uint8_t *buf, size_t len) {
    memcpy(slave_read_buf, buf, len);
    return i2c_master_slave_enable_slave_read(len);
}

//These functions are used to control gpio outputs
int port_signpost_gpio_enable_output(unsigned pin) {
    return gpio_enable_output(pin);
}

int port_signpost_gpio_set(unsigned pin) {
    return gpio_set(pin);
}

int port_signpost_gpio_clear(unsigned pin) {
     return gpio_clear(pin);
}

int port_signpost_gpio_read(unsigned pin) {
     return gpio_read(pin);
}

int port_signpost_debug_led_on(unsigned pin) {
    return led_on(pin);
}
int port_signpost_debug_led_off(unsigned pin){
    return led_off(pin);
}

//This function is used to get the input interrupt for the falling edge of
//mod-in
int port_signpost_gpio_enable_interrupt(unsigned pin, InputMode input_mode, InterruptMode interrupt_mode, port_signpost_callback cb) {
    int rc = 0;
    global_gpio_interrupt_cb = cb;
    rc = gpio_interrupt_callback(port_signpost_gpio_interrupt_callback, NULL);
    if (rc < 0) return rc;
    return gpio_enable_interrupt(pin, (GPIO_InputMode_t) input_mode, (GPIO_InterruptMode_t) interrupt_mode);
}

int port_signpost_gpio_disable_interrupt(unsigned pin) {
    return gpio_disable_interrupt(pin);
}

void port_signpost_wait_for(void* wait_on_true){
    yield_for(wait_on_true);
}

void port_signpost_delay_ms(unsigned ms) {
    delay_ms(ms);
}
