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

// i2c message storage
uint8_t send_buf[20];

#define LED_PIN 0

uint8_t motion_since_last_transmit = 0;
uint32_t max_speed_since_last_transmit = 0;
uint32_t max_index_since_last_transmit = 0;
uint32_t periods_with_motion_since_last_transmit = 0;

static void motion_callback (
        int callback_type __attribute__ ((unused)),
        int pin_value __attribute__ ((unused)),
        int unused __attribute__ ((unused)),
        void* callback_args __attribute__ ((unused))) {

    int motion = mr_is_motion();
    uint32_t motion_index = mr_motion_index();
    uint32_t motion_freq = mr_motion_frequency_hz();
    uint32_t mmps = mr_frequency_to_speed_mmps(motion_freq);

    printf("Motion: %d, Index: %lu, Freq: %lu, Speed: %lu\n",motion,motion_index,motion_freq,mmps);

    if(motion){
        motion_since_last_transmit = 1;
        periods_with_motion_since_last_transmit += 1;
    }

    if(motion_index > max_index_since_last_transmit) {
        max_index_since_last_transmit = motion_index;
    }

    if(mmps > max_speed_since_last_transmit && motion) {
        max_speed_since_last_transmit = mmps;
    }
}

static void timer_callback (
        int callback_type __attribute__ ((unused)),
        int pin_value __attribute__ ((unused)),
        int unused __attribute__ ((unused)),
        void* callback_args __attribute__ ((unused))
        ) {


    // set data
    // boolean, motion since last transmission
    send_buf[1] = (motion_since_last_transmit & 0xFF);
    // uint32_t, max speed in milli-meters per second detected since last transmission
    send_buf[3] = ((max_speed_since_last_transmit >> 24) & 0xFF);
    send_buf[4] = ((max_speed_since_last_transmit >> 16) & 0xFF);
    send_buf[5] = ((max_speed_since_last_transmit >>  8) & 0xFF);
    send_buf[6] = ((max_speed_since_last_transmit)       & 0xFF);
    send_buf[7] = ((max_index_since_last_transmit >> 24) & 0xFF);
    send_buf[8] = ((max_index_since_last_transmit >> 16) & 0xFF);
    send_buf[9] = ((max_index_since_last_transmit >>  8) & 0xFF);
    send_buf[10] = ((max_index_since_last_transmit)       & 0xFF);
    send_buf[11] = ((periods_with_motion_since_last_transmit >> 24) & 0xFF);
    send_buf[12] = ((periods_with_motion_since_last_transmit >> 16) & 0xFF);
    send_buf[13] = ((periods_with_motion_since_last_transmit >>  8) & 0xFF);
    send_buf[14] = ((periods_with_motion_since_last_transmit)       & 0xFF);


    // write data
    int rc = signpost_networking_send("lab11/radar",send_buf,15);
    if(rc >= 0) {
        app_watchdog_tickle_kernel();
        motion_since_last_transmit = 0;
        max_speed_since_last_transmit = 0;
        max_index_since_last_transmit = 0;
        periods_with_motion_since_last_transmit = 0;
    }
}

int main (void) {
    printf("[Microwave Radar] Start\n");

    // initialize LED
    gpio_enable_output(LED_PIN);
    gpio_set(LED_PIN);

    int rc;
    do {
        rc = signpost_initialization_module_init(0x34, NULL);
        if (rc < 0) {
            printf(" - Error initializing bus (code %d). Sleeping for 5s\n",rc);
            delay_ms(5000);
        }
    } while (rc < 0);
    printf(" * Bus initialized\n");

    mr_init();

    send_buf[0] = 0x01;

    // setup two timers. One every 500ms to check for motion and
    // one to send data every 5s summarizing that motion
    static tock_timer_t send_timer;
    timer_every(5000, timer_callback, NULL, &send_timer);

    static tock_timer_t motion_timer;
    timer_every(500, motion_callback, NULL, &motion_timer);

    // Setup a watchdog
    app_watchdog_set_kernel_timeout(60000);
    app_watchdog_start();
}

