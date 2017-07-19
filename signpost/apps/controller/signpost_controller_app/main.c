#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <i2c_master_slave.h>
#include <timer.h>
#include <tock.h>

#include "app_watchdog.h"
#include "controller.h"
#include "fm25cl.h"
#include "gpio_async.h"
#include "gps.h"
#include "minmea.h"
#include "signpost_api.h"
#include "signpost_energy_monitors.h"

int main (void) {
  printf("[Controller] ** Main App **\n");


  printf("Initializing controller\n");
  signpost_controller_init();


  ////////////////////////////////////////////////
  // Setup watchdog
  //app_watchdog_set_kernel_timeout(30000);
  //app_watchdog_start();

  printf("Everything intialized\n");
}

