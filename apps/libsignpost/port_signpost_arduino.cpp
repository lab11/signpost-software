#include <Arduino.h>
#include <Wire.h>
#include <stdarg.h>
#include "port_signpost.h"

//TODO: Verify/test multi-master support

//TODO: choose mod in, mod out, and debug led pins for arduino
//Current defined values are for the Arduino MKRZero
#define ARDUINO_MOD_OUT 		5
#define ARDUINO_MOD_IN 			4
#define DEBUG_LED 				LED_BUILTIN
#define SIGNPOST_I2C_SPEED 		400000
#define _PRINTF_BUFFER_LENGTH_	64
//TODO: Add error handling and error codes
static void mod_in_callback_helper();
static void slave_listen_callback_helper(int num_bytes);
static void slave_read_callback_helper();

//TwoWire object used solely for Signpost i2c
//TwoWire Wire = TwoWire();

static char g_port_printf_buf[_PRINTF_BUFFER_LENGTH_];

uint8_t g_arduino_i2c_address;
port_signpost_callback g_mod_in_callback = NULL;
port_signpost_callback g_slave_listen_callback = NULL;

uint8_t* g_slave_receive_buf = NULL;
size_t g_slave_receive_buf_max_len = 0;
size_t g_slave_receive_buf_len = 0;

uint8_t* g_slave_transmit_buf = NULL;
size_t g_slave_transmit_buf_len = 0;

int port_signpost_init(uint8_t i2c_address) {
	//Wire.begin(i2c_address);
	//Wire.setClock(SIGNPOST_I2C_SPEED);
	g_arduino_i2c_address = i2c_address;
	pinMode(ARDUINO_MOD_IN, INPUT_PULLUP);
	pinMode(ARDUINO_MOD_OUT, OUTPUT);
	pinMode(DEBUG_LED, OUTPUT);
	Wire.onReceive(slave_listen_callback_helper);
	return 0;
}

int port_signpost_i2c_master_write(uint8_t addr, uint8_t* buf, size_t len) {
	Wire.begin();
	// Wire.setClock(SIGNPOST_I2C_SPEED);
	Wire.beginTransmission(addr);
	int num_written = Wire.write(buf, len);
	Wire.endTransmission();
	return num_written;
}

int port_signpost_i2c_slave_listen(port_signpost_callback cb, uint8_t* buf, size_t max_len) {
	Wire.begin(g_arduino_i2c_address);
	// Wire.setClock(SIGNPOST_I2C_SPEED);
	g_slave_receive_buf = buf;
	g_slave_receive_buf_max_len = max_len;
	g_slave_receive_buf_len = 0;
	return 0;
}

int port_signpost_i2c_slave_read_setup(uint8_t* buf, size_t len) {
	Wire.begin(g_arduino_i2c_address);
	// Wire.setClock(SIGNPOST_I2C_SPEED);
	//Set global i2c transmit buffer to input buffer
	g_slave_transmit_buf = buf;
	g_slave_transmit_buf_len = len;
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

int port_signpost_mod_in_enable_interrupt(port_signpost_callback cb) {
	g_mod_in_callback = cb;
	attachInterrupt(digitalPinToInterrupt(ARDUINO_MOD_IN), mod_in_callback_helper, FALLING);
	return 0;
}

int port_signpost_mod_in_disable_interrupt(void) {
	detachInterrupt(digitalPinToInterrupt(ARDUINO_MOD_IN));
	return 0;
}

//TODO: Implement version that puts Arduino into a sleep state
void port_signpost_wait_for(void* wait_on_true) {
	while (!wait_on_true) {
		continue;
	}
}

void port_signpost_delay_ms(unsigned ms) {
	delay(ms);
}

int port_signpost_debug_led_on() {
	digitalWrite(DEBUG_LED, HIGH);
	return 0;
}

int port_signpost_debug_led_off() {
	digitalWrite(DEBUG_LED, LOW);
	return 0;
}

//Uses pseudo-random number generation 
//TODO: Connect hardware rng to arduino and poll that device for random numbers
int port_rng_sync(uint8_t* buf, uint32_t len, uint32_t num) {
	randomSeed(analogRead(3));
	uint32_t i = 0;
	for (; i < num && i < len; ++i) {
		buf[i] = random(0, 256) & 0xFF;
	}
	return i;
}

int port_printf(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	vsnprintf(g_port_printf_buf, _PRINTF_BUFFER_LENGTH_, fmt, args);
	Serial.print(g_port_printf_buf);
	va_end(args);
	return 0;
}

static void mod_in_callback_helper() {
	//If no callback is assigned, return
	if (g_mod_in_callback == NULL) {
		return;
	}
	(*(g_mod_in_callback)) (0);
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
	//Transfer data from read buffer in Wire object to g_slave_receive_buf, then call custom callback
	for (uint32_t i = 0; i < num_bytes && i < g_slave_receive_buf_max_len; ++i, ++g_slave_receive_buf_len) {
		g_slave_receive_buf[g_slave_receive_buf_len] = Wire.read();
	}
	(*(g_slave_listen_callback)) (num_bytes);
}

static void slave_read_callback_helper() {
	//If no buffer is assigned, return
	if (g_slave_transmit_buf == NULL) {
		return;
	}
	Wire.write(g_slave_transmit_buf, g_slave_transmit_buf_len);
}
