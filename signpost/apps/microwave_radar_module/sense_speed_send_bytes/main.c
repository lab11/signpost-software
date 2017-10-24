#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

#include "tock.h"
#include "adc.h"
#include "alarm.h"
#include "console.h"
#include "timer.h"
#include "gpio.h"
#include "led.h"
#include "app_watchdog.h"
#include "i2c_master_slave.h"
#include "signpost_api.h"
#include "microwave_radar.h"
#include "time.h"

// i2c message storage
uint8_t send_buf[50];

#define LED_PIN 0

static uint32_t utime;
struct tm current_time;

static void timer_callback (
        int callback_type __attribute__ ((unused)),
        int pin_value __attribute__ ((unused)),
        int unused __attribute__ ((unused)),
        void* callback_args __attribute__ ((unused))
        ) {

    static int count = 0;
    uint32_t motion_index = mr_motion_index();
    printf("Got motion index of %lu\n",motion_index);

    // uint8_t of the index/2000 (deterimined analytically - if we are at 512000 there is definitely motion).
    if(motion_index > 510000) {
        send_buf[5+count] = 255;
    } else {
        send_buf[5+count] = (uint8_t)((motion_index/2000) & 0xFF);
    }

    count++;

    if(count == 20) {
        printf("About to send data to radio\n");
        int rc = signpost_networking_send("lab11/radar",send_buf,5+count*4);
        printf("Sent data with return code %d\n\n\n",rc);

        if(rc >= 0) {
            app_watchdog_tickle_kernel();
        }

        count = 0;

        //okay now try to get the time from the controller
        signpost_timelocation_time_t stime;
        rc = signpost_timelocation_get_time(&stime);
        printf("Got time with %d satellites\n",stime.satellite_count);
        if(rc < 0 || stime.satellite_count < 2) {
            printf("Failed to get time - assuming 10 seconds\n");
            utime += 10;
            send_buf[1] = (uint8_t)((utime & 0xff000000) >> 24);
            send_buf[2] = (uint8_t)((utime & 0xff0000) >> 16);
            send_buf[3] = (uint8_t)((utime & 0xff00) >> 8);
            send_buf[4] = (uint8_t)((utime & 0xff));
        } else {
            current_time.tm_year = stime.year - 1900;
            current_time.tm_mon = stime.month - 1;
            current_time.tm_mday = stime.day;
            current_time.tm_hour = stime.hours;
            current_time.tm_min = stime.minutes;
            current_time.tm_sec = stime.seconds;
            current_time.tm_isdst = 0;
            utime = mktime(&current_time);
            send_buf[1] = (uint8_t)((utime & 0xff000000) >> 24);
            send_buf[2] = (uint8_t)((utime & 0xff0000) >> 16);
            send_buf[3] = (uint8_t)((utime & 0xff00) >> 8);
            send_buf[4] = (uint8_t)((utime & 0xff));
        }
    }
}

int main (void) {
    printf("[Microwave Radar] Start\n");

    // initialize LED
    gpio_enable_output(LED_PIN);
    gpio_set(LED_PIN);

    //turn off mr radar
    gpio_enable_output(3);
    gpio_clear(3);

    int rc;
    do {
        rc = signpost_initialization_module_init(0x34, NULL);
        if (rc < 0) {
            printf(" - Error initializing bus (code %d). Sleeping for 5s\n",rc);
            delay_ms(5000);
        }
    } while (rc < 0);
    printf(" * Bus initialized\n");

    //turn on mr radar
    gpio_set(3);
    mr_init();

    send_buf[0] = 0x02;

    // setup two timers. One every 500ms to check for motion and
    // one to send data every 5s summarizing that motion
    static tock_timer_t send_timer;
    timer_every(1000, timer_callback, NULL, &send_timer);

    // Setup a watchdog
    app_watchdog_set_kernel_timeout(60000);
    app_watchdog_start();
}

