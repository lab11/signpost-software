// Runs on the ambient module. Reads sensor data and HTTP Posts it over the
// Signpost API

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// tock includes
#include <isl29035.h>
#include <led.h>
#include <lps25hb.h>
#include <si7021.h>
#include <timer.h>
#include <tock.h>

// signpost includes
#include "app_watchdog.h"
#include "signpost_api.h"
#include "simple_post.h"

// module-specific settings
#define AMBIENT_MODULE_I2C_ADDRESS 0x32
#define AMBIENT_LED1 2

// stored sensor readings
typedef struct {
  int light;
  int temperature;
  unsigned humidity;
  int pressure;
  int err_code;
} Sensor_Data_t;
static Sensor_Data_t samples = {0};

uint8_t message_buf[20] = {0};

// keep track of whether functions succeeded
static bool sample_sensors_successful = true;
static bool post_to_radio_successful = true;

static void sample_sensors (void) {
  // read data from sensors and save locally
  sample_sensors_successful = true;

  printf("Getting pressure\n");
  //get pressure
  int pressure = lps25hb_get_pressure_sync();

  // get light
  printf("Getting light\n");
  int light = 0;
  int err_code = isl29035_read_light_intensity();
  if (err_code < TOCK_SUCCESS) {
    printf("Error reading from light sensor: %d\n", light);
  } else {
    light = err_code;
    err_code = TOCK_SUCCESS;
  }

  // get temperature and humidity
  int temperature = 0;
  unsigned humidity = 0;
  int err = si7021_get_temperature_humidity_sync(&temperature, &humidity);
  if (err < TOCK_SUCCESS) {
    printf("Error reading from temperature/humidity sensor: %d\n", err);
    err_code = err;
  }

  // print readings
  printf("--Sensor readings--\n");
  printf("\tTemperature %d (degrees C * 100)\n", temperature);
  printf("\tPressure: %d (microbars)\n",pressure);
  printf("\tHumidity %d (%%RH * 100)\n", humidity);
  printf("\tLight %d (lux)\n", light);

  // store readings
  samples.light = light;
  samples.temperature = temperature;
  samples.humidity = humidity;
  samples.pressure = pressure;
  samples.err_code = err_code;

  //also put them in the send buffer
  message_buf[2] = (uint8_t) ((temperature >> 8) & 0xFF);
  message_buf[3] = (uint8_t) (temperature & 0xFF);
  message_buf[4] = (uint8_t) ((humidity >> 8) & 0xFF);
  message_buf[5] = (uint8_t) (humidity & 0xFF);
  message_buf[6] = (uint8_t) ((light >> 8) & 0xFF);
  message_buf[7] = (uint8_t) (light & 0xFF);
  message_buf[8] = (uint8_t) ((pressure >> 16) & 0xFF);
  message_buf[9] = (uint8_t) ((pressure >> 8) & 0xFF);
  message_buf[10] = (uint8_t) (pressure & 0xFF);

  // track success
  if (err_code != TOCK_SUCCESS) {
    sample_sensors_successful = false;
  }
}

static void post_to_radio (void) {
  // post sensor data over HTTP and get response
  post_to_radio_successful = true;

  //send radio the data
  printf("--Sendinging data--\n");
  int response = signpost_networking_send_bytes(ModuleAddressRadio, message_buf, 11);
  message_buf[1]++;
  if (response < TOCK_SUCCESS) {
    printf("Error posting: %d\n", response);
    post_to_radio_successful = false;
  } else {
    printf("\tResponse: %d\n", response);
  }
}

int main (void) {
  printf("\n[Ambient Module] Sample and Post\n");

  // initialize module as a part of the signpost bus
  int rc;
  do {
    rc = signpost_initialization_module_init(AMBIENT_MODULE_I2C_ADDRESS, NULL);
    if (rc < 0) {
      printf(" - Error initializing bus (code %d). Sleeping for 5s\n", rc);
      delay_ms(5000);
    }
  } while (rc < 0);
  printf(" * Bus initialized\n");

  message_buf[0] = 0x01;
  message_buf[1] = 0x00;
  // set up watchdog
  // Resets after 30 seconds without a valid response
  app_watchdog_set_kernel_timeout(10000);
  app_watchdog_start();
  printf(" * Watchdog started\n");

  printf(" * Initialization complete\n\n");

  // sample from onboard sensors
  sample_sensors();

  printf("Done sampling sensors\n");
  // send HTTP POST over Signpost API
    do {
        post_to_radio();
        if(post_to_radio_successful == false) {
            delay_ms(1000);
        }

    } while(post_to_radio_successful == false);

  //tell the controlelr to duty cycle for 60s
  printf("requesting duty cycle\n");
  while(true) {
    signpost_energy_duty_cycle(600000);
    delay_ms(1000);
  }
}

