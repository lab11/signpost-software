#include "port_signpost.h"

#define MOD_OUT 0
#define MOD_IN  1
#define PPS     2

#define DEBUG_LED 0 // DEBUG_GPIO1

//All implementations must implement a port_print_buf for signbus layer printing
char port_print_buf[80];

//This function is called upon signpost initialization
//You should use it to set up the i2c interface
int port_signpost_init(uint8_t i2c_address) {
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

}

int port_signpost_mod_out_clear(void) {

}

int port_signpost_mod_in_read(void) {

}

int port_signpost_pps_read(void) {

}

//This function is used to get the input interrupt for the falling edge of
//mod-in
int port_signpost_mod_in_enable_interrupt_falling(port_signpost_callback cb) {

}

//This function is used to get the input interrupt for the rising edge of
//mod-in
int port_signpost_mod_in_enable_interrupt_rising(port_signpost_callback cb) {

}

int port_signpost_mod_in_disable_interrupt(void) {

}

void port_signpost_wait_for(void* wait_on_true){

}

int port_signpost_wait_for_with_timeout(void* wait_on_true, uint32_t ms) {

}

void port_signpost_delay_ms(unsigned ms) {

}

int port_signpost_debug_led_on(void) {

}

int port_signpost_debug_led_off(void){

}

int port_rng_init(void) {
    return SB_PORT_SUCCESS;
}

int port_rng_sync(uint8_t* buf, uint32_t len, uint32_t num) {

}

int port_printf(const char *fmt, ...) {

}
