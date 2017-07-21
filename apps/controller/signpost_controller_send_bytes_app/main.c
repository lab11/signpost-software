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
#include "signpost_energy_policy.h"
#include "signpost_energy_monitors.h"
#include "signpost_controller.h"

#include "bonus_timer.h"

static void get_energy_remaining (void);
static void get_energy_average (void);
static void get_batsol (void);

uint8_t gps_buf[20];
uint8_t energy_buf[20];
uint8_t energy_av_buf[20];
uint8_t batsol_buf[20];

static void watchdog_tickler (int which) {
  static bool gps_tickle = false;
  static bool energy_tickle = false;

  if (which == 1) {
    gps_tickle = true;
  } else {
    energy_tickle = true;
  }

  if (gps_tickle && energy_tickle) {
    app_watchdog_tickle_kernel();

    gps_tickle = false;
    energy_tickle = false;
  }
}


static void get_energy_average (void) {


  //int c_power = signpost_energy_policy_get_controller_average_power()/1000;
  int c_power = 0;
  if(c_power <= 0) {
      c_power = 0;
  }

  energy_av_buf[8] = (((c_power) & 0xFF00) >> 8 );
  energy_av_buf[9] = (((c_power) & 0xFF));
  energy_av_buf[10] = 0;
  energy_av_buf[11] = 0;

  //send this data to the radio module
  for(uint8_t i = 0; i < 8; i++) {
      if(i==3 || i == 4) continue;

      //int mod_energy = signpost_energy_policy_get_module_average_power(i)/1000;
      int mod_energy = 0;

      if(mod_energy <= 0) {
          mod_energy = 0;
      }

      energy_av_buf[2+i*2] = (((mod_energy) & 0xFF00) >> 8);
      energy_av_buf[2+i*2+1] = (((mod_energy) & 0xFF));
  }

  int rc;
  rc = signpost_networking_send_bytes(ModuleAddressRadio,energy_av_buf,18);
  energy_av_buf[1]++;

  if(rc >= 0) {
    // Tickle the watchdog because something good happened.
  }

  watchdog_tickler(2);

}

static void get_energy_remaining (void) {
  


  //send this data to the radio module
  /*energy_buf[2] = (((fram.energy.module_energy_remaining[0]/1000) & 0xFF00) >> 8 );
  energy_buf[3] = (((fram.energy.module_energy_remaining[0]/1000) & 0xFF));
  energy_buf[4] = (((fram.energy.module_energy_remaining[1]/1000) & 0xFF00) >> 8 );
  energy_buf[5] = (((fram.energy.module_energy_remaining[1]/1000) & 0xFF));
  energy_buf[6] = (((fram.energy.module_energy_remaining[2]/1000) & 0xFF00) >> 8 );
  energy_buf[7] = (((fram.energy.module_energy_remaining[2]/1000) & 0xFF));
  energy_buf[8] = (((fram.energy.controller_energy_remaining/1000) & 0xFF00) >> 8 );
  energy_buf[9] = (((fram.energy.controller_energy_remaining/1000) & 0xFF));
  energy_buf[10] = 0;
  energy_buf[11] = 0;
  energy_buf[12] = (((fram.energy.module_energy_remaining[5]/1000) & 0xFF00) >> 8 );
  energy_buf[13] = (((fram.energy.module_energy_remaining[5]/1000) & 0xFF));
  energy_buf[14] = (((fram.energy.module_energy_remaining[6]/1000) & 0xFF00) >> 8 );
  energy_buf[15] = (((fram.energy.module_energy_remaining[6]/1000) & 0xFF));
  energy_buf[16] = (((fram.energy.module_energy_remaining[7]/1000) & 0xFF00) >> 8 );
  energy_buf[17] = (((fram.energy.module_energy_remaining[7]/1000) & 0xFF));*/

  int rc;
  rc = signpost_networking_send_bytes(ModuleAddressRadio,energy_buf,18);
  energy_buf[1]++;

  if(rc >= 0) {
    // Tickle the watchdog because something good happened.
  }

  watchdog_tickler(2);

}

static void get_batsol (void) {
  int battery_voltage = signpost_energy_get_battery_voltage_mv();
  int battery_current = signpost_energy_get_battery_current_ua();
  int battery_energy = (int)((signpost_energy_get_battery_energy_uwh()/BATTERY_VOLTAGE_NOM)/1000.0);
  int battery_percent = (int)(signpost_energy_get_battery_percent_mp()/1000.0);
  int battery_full = (int)((signpost_energy_get_battery_capacity_uwh()/BATTERY_VOLTAGE_NOM)/1000.0);
  int solar_voltage = signpost_energy_get_solar_voltage_mv();
  int solar_current = signpost_energy_get_solar_current_ua();
  printf("/**************************************/\n");
  printf("\tBattery Voltage (mV): %d\tcurrent (uA): %d\n",battery_voltage,battery_current);
  printf("\tBattery remaining (mAh): %d\n",battery_energy);
  printf("\tSolar Voltage (mV): %d\tcurrent (uA): %d\n",solar_voltage,solar_current);
  printf("/**************************************/\n");

  batsol_buf[2] = ((battery_voltage & 0xFF00) >> 8);
  batsol_buf[3] = ((battery_voltage & 0xFF));
  batsol_buf[4] = ((battery_current & 0xFF000000) >> 24);
  batsol_buf[5] = ((battery_current & 0xFF0000) >> 16);
  batsol_buf[6] = ((battery_current & 0xFF00) >> 8);
  batsol_buf[7] = ((battery_current & 0xFF));
  batsol_buf[8] = ((solar_voltage & 0xFF00) >> 8);
  batsol_buf[9] = ((solar_voltage & 0xFF));
  batsol_buf[10] = ((solar_current & 0xFF000000) >> 24);
  batsol_buf[11] = ((solar_current & 0xFF0000) >> 16);
  batsol_buf[12] = ((solar_current & 0xFF00) >> 8);
  batsol_buf[13] = ((solar_current & 0xFF));
  batsol_buf[14] = battery_percent;
  batsol_buf[15] = ((battery_energy & 0xFF00) >> 8);
  batsol_buf[16] = ((battery_energy & 0xFF));
  batsol_buf[17] = ((battery_full & 0xFF00) >> 8);
  batsol_buf[18] = ((battery_full & 0xFF));

  int rc;
  rc = signpost_networking_send_bytes(ModuleAddressRadio,batsol_buf,19);
  batsol_buf[1]++;

  if(rc >= 0) {
    // Tickle the watchdog because something good happened.
  }

  watchdog_tickler(2);
}


int main (void) {
  printf("[Controller] ** Main App **\n");

  printf("Initializing controller\n");
  signpost_controller_init();

  printf("Done initializing\n");

  ///////////////////
  // Local Operations
  // ================
  //
  // Initializations that only touch the controller board
  energy_buf[0] = 0x01;
  energy_buf[1] = 0x00;

  energy_av_buf[0] = 0x04;
  energy_av_buf[1] = 0x00;

  gps_buf[0] = 0x02;
  gps_buf[1] = 0x00;

  batsol_buf[0] = 0x03;
  batsol_buf[1] = 0x00;


  ////////////////////////////////////////////////
  // Setup watchdog
  app_watchdog_set_kernel_timeout(180000);
  app_watchdog_start();

  printf("Everything intialized\n");

  uint32_t index = 1;
  while(1) {

    // get energy updates every 10 seconds
    if ((index % 60) == 0) {
      printf("CONTROLLER_STATE: Check energy\n");
      get_energy_remaining();
    }

    if((index %60) == 20) {
      get_batsol();
    }

    if ((index % 60) == 40) {
      printf("CONTROLLER_STATE: Check energy\n");
      get_energy_average();
    }

    index++;
    delay_ms(1000);
  }
}

