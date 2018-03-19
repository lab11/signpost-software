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

static time_t utime;

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
        int rc = signpost_networking_publish("motion",send_buf,5+count*4);
        printf("Sent data with return code %d\n\n\n",rc);

        if(rc >= 0) {
            app_watchdog_tickle_kernel();
        }

        count = 0;

        //okay now try to get the time from the controller
        rc = signpost_timelocation_get_time(&utime);
        if(rc < 0) {
            printf("Failed to get time - assuming 20 seconds\n");
            utime += 20;
            send_buf[1] = (uint8_t)((utime & 0xff000000) >> 24);
            send_buf[2] = (uint8_t)((utime & 0xff0000) >> 16);
            send_buf[3] = (uint8_t)((utime & 0xff00) >> 8);
            send_buf[4] = (uint8_t)((utime & 0xff));
        } else {
            printf("Got time with %d satellites\n",rc);
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
    led_on(2);

    //turn off mr radar
    gpio_enable_output(3);
    gpio_clear(3);

    int rc;
    do {
        rc = signpost_init("lab11/radar");
        if (rc < 0) {
            printf(" - Error initializing bus (code %d). Sleeping for 5s\n",rc);
            delay_ms(5000);
        }
    } while (rc < 0);
    printf(" * Bus initialized\n");

    led_off(2);

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

