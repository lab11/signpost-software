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

#include "controller.h"
#include "fm25cl.h"
#include "gpio_async.h"
#include "gps.h"
#include "minmea.h"
#include "signpost_api.h"
#include "signpost_energy_policy.h"
#include "signpost_energy_monitors.h"
#include "signpost_controller.h"

uint8_t gps_buf[20];
uint8_t energy_buf[60];

static void send_gps_update (__attribute__((unused)) int now,
                                __attribute__((unused)) int experation,
                                __attribute__((unused)) int unused,
                                __attribute__((unused)) void* ud) {

  signpost_timelocation_time_t time;
  signpost_timelocation_location_t location;

  printf("Getting time location\n");
  signpost_controller_get_gps(&time,&location);

  gps_buf[2] = time.day;
  gps_buf[3] = time.month;
  gps_buf[4] = time.year;
  gps_buf[5] = time.hours;
  gps_buf[6] = time.minutes;
  gps_buf[7] = time.seconds;
  gps_buf[8] = ((location.latitude & 0xFF000000) >> 24);
  gps_buf[9] = ((location.latitude & 0x00FF0000) >> 16);
  gps_buf[10] = ((location.latitude & 0x0000FF00) >> 8);
  gps_buf[11] = ((location.latitude & 0x000000FF));
  gps_buf[12] = ((location.longitude & 0xFF000000) >> 24);
  gps_buf[13] = ((location.longitude & 0x00FF0000) >> 16);
  gps_buf[14] = ((location.longitude & 0x0000FF00) >> 8);
  gps_buf[15] = ((location.longitude & 0x000000FF));
  if(time.satellite_count >= 3) {
    gps_buf[16] = 0x02;
  } else if (time.satellite_count >=4) {
    gps_buf[16] = 0x03;
  } else {
    gps_buf[16] = 0x01;
  }
  gps_buf[17] = time.satellite_count;
  printf("Sending GPS packet\n");
  int rc = signpost_networking_send_bytes(ModuleAddressRadio,gps_buf,18);
  if(rc < 0) printf("Error sending GPS packet\n");
  else {
    signpost_controller_app_watchdog_tickle();
    signpost_controller_hardware_watchdog_tickle();
  }
  gps_buf[1]++;
}

static void send_energy_update (__attribute__((unused)) int now,
                                __attribute__((unused)) int experation,
                                __attribute__((unused)) int unused,
                                __attribute__((unused)) void* ud) {

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

  energy_buf[2] = ((battery_voltage & 0xFF00) >> 8);
  energy_buf[3] = ((battery_voltage & 0xFF));
  energy_buf[4] = ((battery_current & 0xFF000000) >> 24);
  energy_buf[5] = ((battery_current & 0xFF0000) >> 16);
  energy_buf[6] = ((battery_current & 0xFF00) >> 8);
  energy_buf[7] = ((battery_current & 0xFF));
  energy_buf[8] = ((solar_voltage & 0xFF00) >> 8);
  energy_buf[9] = ((solar_voltage & 0xFF));
  energy_buf[10] = ((solar_current & 0xFF000000) >> 24);
  energy_buf[11] = ((solar_current & 0xFF0000) >> 16);
  energy_buf[12] = ((solar_current & 0xFF00) >> 8);
  energy_buf[13] = ((solar_current & 0xFF));
  energy_buf[14] = battery_percent;
  energy_buf[15] = ((battery_energy & 0xFF00) >> 8);
  energy_buf[16] = ((battery_energy & 0xFF));
  energy_buf[17] = ((battery_full & 0xFF00) >> 8);
  energy_buf[18] = ((battery_full & 0xFF));

  signpost_energy_remaining_t rem;
  printf("/**************************************/\n");
  rem.controller_energy_remaining = signpost_energy_policy_get_controller_energy_remaining_uwh();
  printf("\tController Energy Remaining: %d uWh\n",rem.controller_energy_remaining);
  for(uint8_t i = 0; i < 8; i++) {
    if(i == 3 || i ==4) continue;

    rem.module_energy_remaining[i] = signpost_energy_policy_get_module_energy_remaining_uwh(i);
    printf("\t\tModule %d Energy Remaining: %d uWh\n",i,rem.module_energy_remaining[i]);
  }
  printf("/**************************************/\n");

  //send this data to the radio module
  energy_buf[19] = (((rem.module_energy_remaining[0]/1000) & 0xFF00) >> 8 );
  energy_buf[20] = (((rem.module_energy_remaining[0]/1000) & 0xFF));
  energy_buf[21] = (((rem.module_energy_remaining[1]/1000) & 0xFF00) >> 8 );
  energy_buf[22] = (((rem.module_energy_remaining[1]/1000) & 0xFF));
  energy_buf[23] = (((rem.module_energy_remaining[2]/1000) & 0xFF00) >> 8 );
  energy_buf[24] = (((rem.module_energy_remaining[2]/1000) & 0xFF));
  energy_buf[25] = (((rem.controller_energy_remaining/1000) & 0xFF00) >> 8 );
  energy_buf[26] = (((rem.controller_energy_remaining/1000) & 0xFF));
  energy_buf[27] = 0;
  energy_buf[28] = 0;
  energy_buf[29] = (((rem.module_energy_remaining[5]/1000) & 0xFF00) >> 8 );
  energy_buf[30] = (((rem.module_energy_remaining[5]/1000) & 0xFF));
  energy_buf[31] = (((rem.module_energy_remaining[6]/1000) & 0xFF00) >> 8 );
  energy_buf[32] = (((rem.module_energy_remaining[6]/1000) & 0xFF));
  energy_buf[33] = (((rem.module_energy_remaining[7]/1000) & 0xFF00) >> 8 );
  energy_buf[34] = (((rem.module_energy_remaining[7]/1000) & 0xFF));
  
  //signpost energy average
  signpost_energy_average_t av;
  printf("/**************************************/\n");
  av.controller_energy_average = signpost_energy_policy_get_controller_energy_average_uw();
  printf("\tController Energy Average: %d uW\n",av.controller_energy_average);
  for(uint8_t i = 0; i < 8; i++) {
    if(i == 3 || i ==4) continue;

    av.module_energy_average[i] = signpost_energy_policy_get_module_energy_average_uw(i);
    printf("\t\tModule %d Energy Average: %d uW\n",i,av.module_energy_average[i]);
  }
  printf("/**************************************/\n");
  energy_buf[35] = (((av.module_energy_average[0]/1000) & 0xFF00) >> 8 );
  energy_buf[36] = (((av.module_energy_average[0]/1000) & 0xFF));
  energy_buf[37] = (((av.module_energy_average[1]/1000) & 0xFF00) >> 8 );
  energy_buf[38] = (((av.module_energy_average[1]/1000) & 0xFF));
  energy_buf[39] = (((av.module_energy_average[2]/1000) & 0xFF00) >> 8 );
  energy_buf[40] = (((av.module_energy_average[2]/1000) & 0xFF));
  energy_buf[41] = (((av.controller_energy_average/1000) & 0xFF00) >> 8 );
  energy_buf[42] = (((av.controller_energy_average/1000) & 0xFF));
  energy_buf[43] = 0;
  energy_buf[44] = 0;
  energy_buf[45] = (((av.module_energy_average[5]/1000) & 0xFF00) >> 8 );
  energy_buf[46] = (((av.module_energy_average[5]/1000) & 0xFF));
  energy_buf[47] = (((av.module_energy_average[6]/1000) & 0xFF00) >> 8 );
  energy_buf[48] = (((av.module_energy_average[6]/1000) & 0xFF));
  energy_buf[49] = (((av.module_energy_average[7]/1000) & 0xFF00) >> 8 );
  energy_buf[50] = (((av.module_energy_average[7]/1000) & 0xFF));


  int rc;
  printf("Sending energy update packet\n");
  rc = signpost_networking_send_bytes(ModuleAddressRadio,energy_buf,51);
  energy_buf[1]++;

  if(rc < 0) printf("Error sending energy packet\n");
  else {
    signpost_controller_app_watchdog_tickle();
    signpost_controller_hardware_watchdog_tickle();
  }

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

  gps_buf[0] = 0x02;
  gps_buf[1] = 0x00;

  printf("Everything intialized\n");


  //setup timers to send status updates to the radio
  static tock_timer_t energy_send_timer;
  timer_every(60000, send_energy_update, NULL, &energy_send_timer);
  delay_ms(30000);
  static tock_timer_t gps_send_timer;
  timer_every(60000, send_gps_update, NULL, &gps_send_timer);
}

