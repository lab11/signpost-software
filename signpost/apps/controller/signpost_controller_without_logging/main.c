#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "controller.h"
#include "signpost_controller.h"

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

