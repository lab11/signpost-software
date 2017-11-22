// Runs on the ambient module. Reads sensor data and HTTP Posts it over the
// Signpost API

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// tock includes
#include <ambient_light.h>
#include <humidity.h>
#include <led.h>
#include <temperature.h>
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
  int err_code;
} Sensor_Data_t;
static Sensor_Data_t samples = {0};

// keep track of whether functions succeeded
static bool sample_sensors_successful = true;
static bool post_over_http_successful = true;

static void sample_sensors (void) {
  // read data from sensors and save locally
  sample_sensors_successful = true;

  // get light
  int light = 0;
  int err_code = ambient_light_read_intensity();
  if (err_code < TOCK_SUCCESS) {
    printf("Error reading from light sensor: %d\n", light);
  } else {
    light = err_code;
    err_code = TOCK_SUCCESS;
  }

  // get temperature and humidity
  int temperature = 0;
  unsigned humidity = 0;
  int err = humidity_read_sync(&humidity);
  err |= temperature_read_sync(&temperature);

  if (err < TOCK_SUCCESS) {
    printf("Error reading from temperature/humidity sensor: %d\n", err);
    err_code = err;
  }

  // print readings
  printf("--Sensor readings--\n");
  printf("\tTemperature %d (degrees C * 100)\n", temperature);
  printf("\tHumidity %d (%%RH * 100)\n", humidity);
  printf("\tLight %d (lux)\n", light);

  // store readings
  samples.light = light;
  samples.temperature = temperature;
  samples.humidity = humidity;
  samples.err_code = err_code;

  // track success
  if (err_code != TOCK_SUCCESS) {
    sample_sensors_successful = false;
  }
}

static void post_over_http (void) {
  // post sensor data over HTTP and get response
  post_over_http_successful = true;

  // URL for an HTTP POST testing service
  const char* url = "httpbin.org/post";

  // http post data
  printf("--POSTing data--\n");
  int response = simple_octetstream_post(url, (uint8_t*)&samples, sizeof(Sensor_Data_t));
  if (response < TOCK_SUCCESS) {
    printf("Error posting: %d\n", response);
    post_over_http_successful = false;
  } else {
    printf("\tResponse: %d\n", response);
  }
}

static void tickle_watchdog (void) {
  // keep the watchdog from resetting us if everything is successful

  if (sample_sensors_successful && post_over_http_successful) {
    app_watchdog_tickle_kernel();
    led_toggle(AMBIENT_LED1);
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

  // set up watchdog
  // Resets after 30 seconds without a valid response
  app_watchdog_set_kernel_timeout(30000);
  app_watchdog_start();
  printf(" * Watchdog started\n");

  printf(" * Initialization complete\n\n");

  // perform main application
  while (true) {
    // sample from onboard sensors
    sample_sensors();

    // send HTTP POST over Signpost API
    post_over_http();

    // check the watchdog
    tickle_watchdog();
    printf("\n");

    // sleep for a bit
    delay_ms(3000);
  }
}

