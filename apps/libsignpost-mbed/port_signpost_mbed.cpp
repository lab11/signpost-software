//stdlib
#include <stdio.h>

//mbed os
#include "mbed.h"

//signpost port layer
#include "port_signpost.h"

//pin definitions from each board
#include "board.h"

//Global definitions for hardware interfaces
//We define a serial so that print works
static DigitalOut Debug(DEBUG_LED);
static DigitalOut ModOut(MOD_OUT);
static InterruptIn ModIn(MOD_IN);
static DigitalIn pps(PPS);
static Serial DBG(SERIAL_TX, SERIAL_RX, 115200);
static I2CSlave* I2Creader = NULL;
static uint8_t address;
static Mutex slaveMutex;

//All implementations must implement a port_print_buf for signbus layer printing
char port_print_buf[80];

//This function is called upon signpost initialization
//You should use it to set up the i2c interface
int port_signpost_init(uint8_t i2c_address) {
    address = i2c_address << 1;
    ModIn.mode(PullUp);
    I2Creader = new I2CSlave(I2C_MASTER_SDA, I2C_MASTER_SCL);
    I2Creader->address(address);
    return SB_PORT_SUCCESS;
}

//This function is a blocking i2c send call
//it should return the length of the message successfully sent on the bus
//If the bus returns an error, use the appropriate error code
//defined in this file
int port_signpost_i2c_master_write(uint8_t dest, uint8_t* buf, size_t len) {
    dest = dest << 1;

    slaveMutex.lock();

    //delete the I2CSlave if it exists
    if(I2Creader != NULL){
        delete I2Creader;
    }

    I2C* I2Cwriter = new I2C(I2C_MASTER_SDA, I2C_MASTER_SCL);
    int rc = I2Cwriter->write(dest, (const char*)buf,len);
    delete I2Cwriter;

    //create the reader again
    I2Creader = new I2CSlave(I2C_MASTER_SDA, I2C_MASTER_SCL);
    I2Creader->address(address);

    slaveMutex.unlock();

    if(rc != 0) {
        return SB_PORT_FAIL;
    } else {
        return SB_PORT_SUCCESS;
    }
}

//global variables related to listening for i2c transactions
static port_signpost_callback listen_cb = NULL;
static uint8_t* listen_buf;
static size_t listen_len;
static uint8_t* read_buf;
static size_t read_len;

static void i2c_listen_loop() {
    while(1) {
        slaveMutex.lock();
        int i = I2Creader->receive();
        switch (i) {
        case I2CSlave::ReadAddressed:
            I2Creader->write((const char*)read_buf, read_len);
            Thread::signal_wait(0x01);
        break;
        case I2CSlave::WriteAddressed:
            I2Creader->read((char*)listen_buf, listen_len);
            uint16_t len = (listen_buf[4] << 8) + listen_buf[5];
            if(len < listen_len) {
                listen_cb(len);
            } else {
                listen_cb(SB_PORT_FAIL);
            }
            Thread::signal_wait(0x01);
        break;
        }
        slaveMutex.unlock();
    }
}

//This function sets up the asynchronous i2c receive interface
//When this function is called start listening on the i2c bus for
//The address specified in init
//Place data in the buffer no longer than the max len
int port_signpost_i2c_slave_listen(port_signpost_callback cb, uint8_t* buf, size_t max_len) {
    listen_cb = cb;
    listen_buf = buf;
    listen_len = max_len;

    //spawn a listener thread that sends callbacks through the port_signpost_callback
    //declare a thread
    static Thread listenerThread;
    if(listenerThread.get_state() == Thread::Inactive
            || listenerThread.get_state() == Thread::Ready
            || listenerThread.get_state() == Thread::Deleted) {
        listenerThread.set_priority(osPriorityBelowNormal);
        if(listenerThread.start(i2c_listen_loop) == osOK) {
            return SB_PORT_SUCCESS;
        } else {
            return SB_PORT_FAIL;
        }
    } else {
        listenerThread.signal_set(0x01);
        return SB_PORT_SUCCESS;
    }
}

int port_signpost_i2c_slave_read_setup(uint8_t *buf, size_t len) {
    read_buf = buf;
    read_len = len;
    return SB_PORT_SUCCESS;
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
    return SB_PORT_SUCCESS;
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
    return SB_PORT_SUCCESS;
}

int port_signpost_mod_in_disable_interrupt(void) {
    ModIn.disable_irq();
    falling_cb = NULL;
    rising_cb = NULL;
    return SB_PORT_SUCCESS;
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
    return SB_PORT_SUCCESS;
}

int port_signpost_debug_led_off(void){
    Debug = 0;
    return SB_PORT_SUCCESS;
}

int port_rng_init(void) {
    return SB_PORT_SUCCESS;
}

int port_rng_sync(uint8_t* buf, uint32_t len, uint32_t num) {
    //for right now mbed doesn't have an RNG implemented (sigh)
    //we will just use rand for now

    for(uint32_t i = 0; i < num && i < len; i++) {
        buf[i] = (rand() & 0xff);
    }

    return num;
}

int port_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int rc = vprintf(fmt, args);
    va_end(args);
    return rc;
}
