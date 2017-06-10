#include "Arduino.h"
#include <Wire.h>
#include "port_signpost.h"

//TODO: choose mod in, mod out, and debug led pins for arduino
#define ARDUINO_MOD_OUT
#define ARDUINO_MOD_IN
#define DEBUG_LED
#define SIGNPOST_I2C_SPEED 400000

//TODO: Add error handling and error codes

static void slave_listen_callback_helper(int num_bytes);
static void mod_in_callback_helper();

uint8_t g_arduino_i2c_address;
port_signpost_callback g_mod_in_callback = NULL;
port_signpost_callback g_slave_listen_callback = NULL;

uint8_t* g_slave_receive_buf = NULL;
size_t g_slave_receive_buf_max_size = 0;
size_t g_slave_receive_buf_len = 0;

int port_signpost_init(uint8_t i2c_address) {
	Wire.begin();
	Wire.setClock(SIGNPOST_I2C_SPEED);
	g_arduino_i2c_address = i2c_address;
	pinMode(ARDUINO_MOD_IN, INPUT_PULLUP);
	pinMode(ARDUINO_MOD_OUT, OUTPUT);
	pinMode(DEBUG_LED, OUTPUT);
	Wire.onReceive(slave_listen_callback_helper);
	return 0;
}

int port_signpost_i2c_master_write(uint8_t addr, uint8_t* buf, size_t len) {
	Wire.begin();
	Wire.setClock(SIGNPOST_I2C_SPEED);
	Wire.beginTransmission(addr);
	int num_written = Wire.write(buf, len);
	Wire.endTransmission(stop);
	return num_written;
}

int port_signpost_i2c_slave_listen(port_signpost_callback cb, uint8_t* buf, size_t max_len) {
	Wire.begin(g_arduino_i2c_address);
	Wire.setClock(SIGNPOST_I2C_SPEED);
	g_slave_receive_buf = buf;
	g_slave_receive_buf_max_size = max_len;
	g_slave_receive_buf_len = 0;
	return 0;
}

int port_signpost_i2c_slave_read_setup(uint8_t* buf, size_t len) {

}

int port_signpost_mod_out_set(void) {
	digitalWrite(ARDUINO_MOD_OUT, HIGH);
	return 0;
}

int port_signpost_mod_out_clear(void) {
	digitalWrite(ARDUINO_MOD_OUT, LOW);
	return 0;
}

int port_signpost_mod_in_read(void) {
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

void port_signpost_wait_for(void* wait_on_true) {

}

void port_signpost_delay_ms(unsigned ms) {
	delay(ms);
}

int port_signpost_debug_led_on(void) {
	digitalWrite(DEBUG_LED, HIGH);
	return 0;
}

int port_signpost_debug_led_off(void) {
	digitalWrite(DEBUG_LED, LOW);
	return 0;
}

static void mod_in_callback_helper() {
	//If no callback is assigned, return
	if (g_mod_in_callback == NULL) {
		return;
	}
	*(g_mod_in_callback) (0);
}
//TODO: Add condition for overflow of g_slave_receive_buf
static void slave_listen_callback_helper(int num_bytes) {
	//If no callback is assigned, return
	if (g_slave_listen_callback == NULL) {
		return;
	}
	//Transfer data from read buffer in Wire object to g_slave_receive_buf, then call custom callback
	for (uint i = 0; i < num_bytes && i < g_slave_receive_buf_max_len; ++i, ++g_slave_receive_buf_len) {
		g_slave_receive_buf[i] = Wire.read();
	}
	*(g_slave_listen_callback) (num_bytes);
}