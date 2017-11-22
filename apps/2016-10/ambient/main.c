#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

#include "tock.h"
#include "console.h"
#include "lps331ap.h"
#include "i2c_master_slave.h"
#include "humidity.h"
#include "temperature.h"
#include "ambient_light.h"
#include "timer.h"
#include "led.h"
#include "app_watchdog.h"

uint8_t txbuf[32] = {0};

#define _U __attribute__ ((unused))

static void print_measurements (int temp, int humi, int pres, int ligh) {
  printf("[Ambient] Got Measurements\n");

  // Temperature and Humidity
  printf("  Temp(%d 1/100 degrees C) [0x%X]\n", temp, temp);
  printf("  Humi(%d 0.01%%) [0x%X]\n", humi, humi);

  // Print the pressure value
  printf("  Pressure(%d ubar) [0x%X]\n", pres, pres);

  // Light
  printf("  Light(%d) [0x%X]\n", ligh, ligh);
}

static void sample_and_send (void) {
  // Start a pressure measurement
  int pressure = lps331ap_get_pressure_sync();
  // Get light
  int light = ambient_light_read_intensity();
  // Get temperature and humidity
  int temperature;
  unsigned humidity;
  //si7021_get_temperature_humidity_sync(&temperature, &humidity);
  humidity_read_sync(&humidity);
  temperature_read_sync(&temperature);


  // Encode readings in txbuf
  txbuf[2] = (uint8_t) ((temperature >> 8) & 0xFF);
  txbuf[3] = (uint8_t) (temperature & 0xFF);
  txbuf[4] = (uint8_t) ((humidity >> 8) & 0xFF);
  txbuf[5] = (uint8_t) (humidity & 0xFF);
  txbuf[6] = (uint8_t) ((light >> 8) & 0xFF);
  txbuf[7] = (uint8_t) (light & 0xFF);
  txbuf[8] = (uint8_t) ((pressure >> 16) & 0xFF);
  txbuf[9] = (uint8_t) ((pressure >> 8) & 0xFF);
  txbuf[10] = (uint8_t) (pressure & 0xFF);

  print_measurements(temperature, humidity, pressure, light);

  int result = i2c_master_slave_write_sync(0x22, 11);
  if (result >= 0) {
    app_watchdog_tickle_kernel();
    led_toggle(0);
  } else {
    printf("I2C Write: %i\n", result);
  }
}

static void timer_callback (int callback_type _U, int pin_value _U, int unused _U, void* callback_args _U) {
  sample_and_send();
}

int main (void) {
  printf("[Ambient] Measure and Report\n");

  // Setup I2C TX buffer
  txbuf[0] = 0x32; // My address
  txbuf[1] = 0x01; // Message type

  // Set buffer for I2C messages
  i2c_master_slave_set_master_write_buffer(txbuf, 32);

  // Set our address in case anyone cares
  i2c_master_slave_set_slave_address(0x32);

  // sample immediately
  sample_and_send();

  // Setup a timer for sampling the sensors
  static tock_timer_t timer;
  timer_every(6000, timer_callback, NULL, &timer);

  // Setup watchdog
  app_watchdog_set_kernel_timeout(10000);
  app_watchdog_start();
}
