#include <stdarg.h>

#include <Arduino.h>
#include <HardwareSerial.h>
#include <Wire.h>

#include "port_signpost.h"
#include "signpost_entropy.h"

//Defines for Arduino pins
//Current values are defined for the Arduino MKRZero
#define ARDUINO_MOD_IN 	4
#define ARDUINO_MOD_OUT	5
#define ARDUINO_PPS		3 //placeholder pin
#define DEBUG_LED 		LED_BUILTIN

#define SIGNPOST_I2C_SPEED 		400000
#define _PRINTF_BUFFER_LENGTH_	512

//enable/disable printing debug messages over uart
#define ENABLE_ARDUINO_PORT_PRINTF

//TODO: Add error handling and error codes
//Callback helper functions
//Function prototypes for Arduino callbacks and signpost callbacks differ,
//so helper functions are used as an interface between the two.
//The arduino hardware interrupt calls the helper function, which then calls the 
//signpost callback.
static void mod_in_callback_falling_helper();
static void mod_in_callback_rising_helper();
static void slave_listen_callback_helper(int num_bytes);
static void slave_read_callback();

//TODO: Implement separate i2c bus solely for use with signpost.

//Make sure to set i2c buffer size to 256 within hardware i2c library
//For a regular avr arduino, this is located in twi.h
//For the arduino MKRZero, this is located in RingBuffer.h
//This should be done automatically when building the Arduino hardware package

//Buffer for port_printf
static char g_port_printf_buf[_PRINTF_BUFFER_LENGTH_];
//i2c address of arduino module
static uint8_t g_arduino_i2c_address;
//Callbacks for port layer functions
static port_signpost_callback g_mod_in_callback_falling = NULL;
static port_signpost_callback g_mod_in_callback_rising = NULL;
static port_signpost_callback g_slave_listen_callback = NULL;
//Slave receive buffer
static uint8_t* g_slave_receive_buf = NULL;
static size_t g_slave_receive_buf_max_len = 0;
static size_t g_slave_receive_buf_len = 0;
//Slave transmission buffer
static uint8_t* g_slave_transmit_buf = NULL;
static size_t g_slave_transmit_buf_len = 0;

int port_signpost_init(uint8_t i2c_address) {
	//Initialize i2c as slave
	Wire.begin(i2c_address);
	g_arduino_i2c_address = i2c_address;
	//Setup GPIO pins
	pinMode(ARDUINO_MOD_IN, INPUT);
	pinMode(ARDUINO_MOD_OUT, OUTPUT);
	pinMode(ARDUINO_PPS, INPUT);
	pinMode(DEBUG_LED, OUTPUT);
	//Set callback helpers for receiving and transmitting i2c message in slave mode
	Wire.onReceive(slave_listen_callback_helper);
	Wire.onRequest(slave_read_callback);
	return 0;
}

int port_signpost_i2c_master_write(uint8_t addr, uint8_t* buf, size_t len) {
	if (len == 0 || len > 256) {
		return SB_PORT_ESIZE;
	}
	//Initialize i2c as master
	Wire.begin();
	//Set i2c clock speed
	Wire.setClock(SIGNPOST_I2C_SPEED);
	//Send i2c message to addr
	Wire.beginTransmission(addr);
	int num_written = Wire.write(buf, len);
	int rc = Wire.endTransmission();
	//Put Arduino back into i2c slave mode
	Wire.begin(g_arduino_i2c_address);
	//Check for errors or return number of bytes written if no errors are present
	//Below code unverified on hardware
	switch (rc) {
		case 0:
			return num_written;
		break;
		case 1:
			return SB_PORT_ESIZE;
		break;
		case 2:
			return SB_PORT_ENOACK;
		break;
		case 3:
			return SB_PORT_ENOACK;
		break;
		case 4:
			return SB_PORT_FAIL;
		break;
		default:
			return SB_PORT_FAIL;
		break;
	}
}

int port_signpost_i2c_slave_listen(port_signpost_callback cb, uint8_t* buf, size_t max_len) {
	//Initialize i2c as slave
	Wire.begin(g_arduino_i2c_address);
	//Set i2c slave listen callback and buffer
	g_slave_listen_callback = cb;
	g_slave_receive_buf = buf;
	g_slave_receive_buf_max_len = max_len;
	g_slave_receive_buf_len = 0;
	return 0;
}

//Function untested
int port_signpost_i2c_slave_read_setup(uint8_t* buf, size_t len) {
	//Initialize i2c as slave
	Wire.begin(g_arduino_i2c_address);
	//Set global i2c slave transmit buffer
	g_slave_transmit_buf = buf;
	g_slave_transmit_buf_len = len;
	return 0;
}

int port_signpost_mod_out_set() {
	digitalWrite(ARDUINO_MOD_OUT, HIGH);
	return 0;
}

int port_signpost_mod_out_clear() {
	digitalWrite(ARDUINO_MOD_OUT, LOW);
	return 0;
}

int port_signpost_mod_in_read() {
	return digitalRead(ARDUINO_MOD_IN);
}

int port_signpost_mod_in_enable_interrupt_falling(port_signpost_callback cb) {
	//Set global MOD_IN falling edge callback 
	g_mod_in_callback_falling = cb;
	//Attach callback helper to hardware interrupt
	attachInterrupt(digitalPinToInterrupt(ARDUINO_MOD_IN), mod_in_callback_falling_helper, FALLING);
	return 0;
}

int port_signpost_mod_in_enable_interrupt_rising(port_signpost_callback cb) {
	//Set global MOD_IN rising edge callback 
	g_mod_in_callback_rising = cb;
	//Attach callback helper to hardware interrupt
	attachInterrupt(digitalPinToInterrupt(ARDUINO_MOD_IN), mod_in_callback_rising_helper, RISING);
	return 0;
}

int port_signpost_mod_in_disable_interrupt(void) {
	detachInterrupt(digitalPinToInterrupt(ARDUINO_MOD_IN));
	return 0;
}

//TODO: Implement version that puts Arduino into a sleep state
void port_signpost_wait_for(void* wait_on_true) {
	volatile bool wait_on_true_volatile = false;
	//Stay in loop until wait_on_true becomes true
	while (!wait_on_true_volatile) {
		wait_on_true_volatile = *((bool*) wait_on_true);
	}
}

//TODO: Implement version that actually times out
//		For now it is just a replica of port_signpost_wait_for
int port_signpost_wait_for_with_timeout(void* wait_on_true, uint32_t ms) {
	port_signpost_wait_for(wait_on_true);
	return SB_PORT_SUCCESS;
}

void port_signpost_delay_ms(unsigned ms) {
	//For every 1 ms delay, call delayMicroseconds once.
	//delayMicroseconds does not work for inputs > 16383,
	//so a loop is used to divide up the duration.
	for (unsigned i = 0; i < ms; ++i) {
		delayMicroseconds(1000);
	}
}

int port_signpost_debug_led_on() {
	digitalWrite(DEBUG_LED, HIGH);
	return 0;
}

int port_signpost_debug_led_off() {
	digitalWrite(DEBUG_LED, LOW);
	return 0;
}

int port_rng_init() {
	if (signpost_entropy_init() < 0) return SB_PORT_FAIL;
	return SB_PORT_SUCCESS;
}

//Uses pseudo-random number generation 
//TODO: Connect hardware rng to arduino and poll that device for random numbers
int port_rng_sync(uint8_t* buf, uint32_t len, uint32_t num) {
	//Generate pseudo-random seed by reading an analog pin
	randomSeed(analogRead(2)); 
	uint32_t i = 0;
	//Generate pseudo-random numbers from seed
	for (; i < num && i < len; ++i) {
		buf[i] = random(0, 256) & 0xFF;
	}
	return i;
}

int port_printf(const char *fmt, ...) {
	#ifdef ENABLE_ARDUINO_PORT_PRINTF
		va_list args;
		va_start(args, fmt);
		int rc = vsnprintf(g_port_printf_buf, _PRINTF_BUFFER_LENGTH_, fmt, args);
		Serial.print(g_port_printf_buf);
		va_end(args);
		return rc;
	#else
		return 0;
	#endif
}

static void mod_in_callback_falling_helper() {
	//If no callback is assigned, return
	if (g_mod_in_callback_falling == NULL) {
		return;
	}
	//Call callback assigned in port_signpost_mod_in_enable_interrupt_falling
	(*(g_mod_in_callback_falling)) (0);
}

static void mod_in_callback_rising_helper() {
	//If no callback is assigned, return
	if (g_mod_in_callback_rising == NULL) {
		return;
	}
	//Call callback assigned in port_signpost_mod_in_enable_interrupt_rising
	(*(g_mod_in_callback_rising)) (0);
}

//TODO: Add condition for overflow of g_slave_receive_buf
static void slave_listen_callback_helper(int num_bytes) {
	//If no callback is assigned, return
	if (g_slave_listen_callback == NULL) {
		return;
	}
	//If no buffer is assigned, return
	if (g_slave_receive_buf == NULL) {
		return;
	}
	//Transfer data from read buffer in Wire object to g_slave_receive_buf
	for (uint32_t i = 0; i < num_bytes && i < g_slave_receive_buf_max_len; ++i, ++g_slave_receive_buf_len) {
		g_slave_receive_buf[g_slave_receive_buf_len] = Wire.read();
	}
	//Call callback assigned in port_signpost_i2c_slave_listen
	(*(g_slave_listen_callback)) (num_bytes);
}

//callback untested
static void slave_read_callback() {
	//If no buffer is assigned, return
	if (g_slave_transmit_buf == NULL) {
		return;
	}
	//Write contents of transmission buffer to i2c bus
	Wire.write(g_slave_transmit_buf, g_slave_transmit_buf_len);
}
