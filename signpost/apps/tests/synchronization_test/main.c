#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <alarm.h>
#include <gpio.h>
#include <led.h>
#include <timer.h>
#include <tock.h>

#include "signpost_api.h"

static struct tm* current_time;
static uint8_t new_time = 0;
static uint8_t flash_led = false;

//assuming that no time_location request takes longer than ~1s
//time should be
//if(new_time == 1) {
//time + timer_read
//}
//
static uint32_t last_pps_time = 0;
static uint32_t pin_time = 0;
static uint8_t got_int = 0;

static void timer_callback (__attribute ((unused)) int pin_num,
        __attribute ((unused)) int arg2,
        __attribute ((unused)) int arg3,
        __attribute ((unused)) void* userdata) {
}


static void pps_callback (int pin_num,
        __attribute ((unused)) int arg2,
        __attribute ((unused)) int arg3,
        __attribute ((unused)) void* userdata) {

    if(pin_num == 2) {

        last_pps_time = alarm_read();

        printf("Got PPS interrupt\n");

        //now let's query time from the controller again
        new_time = 0;

        if(flash_led) {
            led_toggle(0);
            flash_led = false;
        }
    } else if (pin_num == 1) {
        pin_time = alarm_read();
        got_int = 1;
        float seconds;
        if(new_time) {
            seconds = current_time->tm_sec + (pin_time - last_pps_time)/16000.0;
        } else {
            seconds = 1 + current_time->tm_sec + (pin_time - last_pps_time)/16000.0;
        }
        printf("Interrupt occurred at %d:%lu\n",current_time->tm_min,(uint32_t)(seconds*1000000));
    }
}

int main (void) {
  printf("\n\n[Test] Synchronization\n");


    //initialize signpost API
  int rc = signpost_init("test","synch");
  if (rc < TOCK_SUCCESS) {
    printf("Signpost initialization errored: %d\n", rc);
  }


  led_off(0);

  printf("Setting up interrupts\n");

  //setup a callback for pps
  gpio_enable_input(2, PullNone);
  gpio_enable_interrupt(2, RisingEdge);
  gpio_enable_input(1, PullNone);
  gpio_enable_interrupt(1, FallingEdge);
  gpio_interrupt_callback(pps_callback, NULL);

  printf("Starting Timer\n");
  //this is just to make sure the timer is running
  static tock_timer_t timer;
  timer_every(2000, timer_callback, NULL, &timer);

  last_pps_time = alarm_read();

  while (true) {
    if(new_time == 0) {
        time_t utime;
        rc = signpost_timelocation_get_time(&utime);
        current_time = gmtime(&utime);
        printf("Got new time %d:%d\n",current_time->tm_min,current_time->tm_sec);
        if(rc >= 0) {
            new_time = 1;
            if((current_time->tm_sec % 10) == 0) {
                flash_led = true;
            }
        }
    }
    yield();
  }
}
