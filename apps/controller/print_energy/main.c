#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <tock.h>
#include <timer.h>

#include "controller.h"
#include "i2c_selector.h"
#include "signpost_energy_monitors.h"

int main (void) {
  uint32_t energy;

  signpost_energy_init_ltc2941();

  signpost_energy_reset_all_energy();

  controller_init_module_switches();
  controller_all_modules_enable_power();
  controller_all_modules_enable_i2c();

  int i;

  while (1) {

    printf("\nChecking Energy\n");

    for (i=0; i<8; i++) {
      if (i == 3 || i == 4) continue;

      energy = signpost_energy_get_module_energy(i);
      printf("Module %d energy: %lu uWh", i, energy);
    }

    energy = signpost_energy_get_controller_energy();
    printf("Controller energy: %lu uWh", energy);

    energy = signpost_energy_get_linux_energy();
    printf("Linux energy: %lu uWh", energy);

    delay_ms(1000);
  }
}
