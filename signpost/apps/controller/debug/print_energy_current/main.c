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
  uint32_t current;

  signpost_energy_init_ltc2943();

  signpost_energy_reset_all_energy();

  controller_init_module_switches();
  controller_all_modules_enable_power();
  controller_all_modules_enable_i2c();

  int i;

  while (1) {

    printf("\nChecking Energy\n");

    for (i=0; i<8; i++) {
      if (i == 3 || i == 4) continue;

      energy = signpost_energy_get_module_energy_uwh(i);
      current = signpost_energy_get_module_current_ua(i);
      printf("Module %d energy: %lu uWh, current: %lu uAh\n",i, energy, current);
    }

    energy = signpost_energy_get_controller_energy_uwh();
    current = signpost_energy_get_controller_current_ua();
    printf("Controller energy: %lu uWh, current: %lu uAh\n",energy, current);

    energy = signpost_energy_get_linux_energy_uwh();
    current = signpost_energy_get_linux_current_ua();
    printf("Linux energy: %lu uWh, current: %lu uAh\n",energy, current);

    uint16_t v = signpost_energy_get_battery_voltage_mv();
    int32_t c = signpost_energy_get_battery_current_ua();
    uint32_t e = signpost_energy_get_battery_energy_uwh();
    uint32_t p = signpost_energy_get_battery_percent_mp();
    uint32_t cap = signpost_energy_get_battery_capacity_uwh();

    uint16_t s_voltage = signpost_energy_get_solar_voltage_mv();
    uint32_t s_current = signpost_energy_get_solar_current_ua();

    printf("Battery Voltage (mV): %hu\tcurrent (uA): %ld\tenergy (uWh):%lu\n",v,c,e);
    printf("Battery Percent: %lu\tcapacity (uWh): %lu\n",p/1000,cap);
    printf("Solar Voltage (mV): %hu\tcurrent (uA): %lu\n",s_voltage,s_current);

    delay_ms(1000);
  }
}
