
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>

#include <tock.h>
#include <console.h>
#include "gpio.h"
#include "led.h"
#include "adc.h"
#include "app_watchdog.h"
#include "msgeq7.h"
#include "i2c_master_slave.h"
#include "timer.h"
#include "signpost_api.h"

#define STROBE 3
#define RESET 4
#define POWER 5
#define GREEN_LED 2
#define RED_LED 3
#define BUFFER_SIZE 20

uint8_t send_buf[100];
uint8_t send_buf2[100];
bool sample_done = false;
bool still_sampling = false;

//gain = 20k resistance
#define PREAMP_GAIN 22.5
#define MSGEQ7_GAIN 22.0
#define SPL 94.0
//the magic number is a combination of:
//voltage ref
//bits of adc precision
//vpp to rms conversion
//microphone sensitivity
#define MAGIC_NUMBER 43.75
#define OTHER_MAGIC 35.5

static uint8_t convert_to_db(uint16_t output) {
    float out;
    if(output == 0) {
        out = 50;
    } else {
        out = (((20*log10(output/MAGIC_NUMBER)) + OTHER_MAGIC));
    }

    if(out < 50) {
        out = 50;
    } else if(out > 75) {
        out = 75;
    }

    out -= 50;

    return (uint8_t)(out*10);
}

static void delay(void) {
    for(volatile uint16_t i = 0; i < 2000; i++);
}

struct tm current_time;

int bands_total[7] = {0};
int bands_max[7] = {0};
int bands_now[7] = {0};
int bands_num[7] =  {0};

static void adc_callback (
        int callback_type __attribute__ ((unused)),
        int pin_value __attribute__ ((unused)),
        int sample,
        void* callback_args __attribute__ ((unused))
        ) {

    static uint8_t i = 0;

    int db = convert_to_db(sample);

    bands_total[i] += db;
    if(sample > bands_max[i]) {
        bands_max[i] = db;
    }
    bands_now[i] = db;
    bands_num[i]++;
    delay();
    gpio_set(STROBE);
    delay();
    gpio_clear(STROBE);

    if(i == 6) {

        if(bands_now[3] > 60) {
            //turn on green LED
            led_on(GREEN_LED);
        } else {
            led_off(GREEN_LED);
        }
        if(bands_now[3] > 60) {
            //turn on red LED
            led_on(RED_LED);
        } else {
            led_off(RED_LED);
        }

        delay();
        gpio_set(STROBE);
        gpio_set(RESET);
        delay();
        gpio_clear(STROBE);
        delay();
        gpio_clear(RESET);
        gpio_set(STROBE);
        delay();
        gpio_clear(STROBE);

        i = 0;
        still_sampling = true;

    } else {

        i++;

    }

    sample_done = true;
}

static time_t utime;

static void timer_callback (
        int callback_type __attribute__ ((unused)),
        int pin_value __attribute__ ((unused)),
        int unused __attribute__ ((unused)),
        void* callback_args __attribute__ ((unused))
        ) {

    int rc;
    static int count = 0;

    //calculate the band averages over the timer period
    //and pack them into the send buf

    for(uint8_t j = 0; j < 7; j++) {
        send_buf[5+count*7+j] = (uint8_t)(((uint8_t)(bands_total[j]/(float)bands_num[j])) & 0xff);
    }

    //reset all the variables for the next period
    for(uint8_t j = 0; j < 7; j++) {
        bands_total[j] = 0;
        bands_now[j] = 0;
        bands_max[j] = 0;
        bands_num[j] = 0;
    }

    printf("Got timer\n");
    count++;

    if(count == 10) {
        memcpy(send_buf2,send_buf,100);
        count = 0;

        printf("About to send data to radio\n");
        rc = signpost_networking_publish("spectrum",send_buf2,75);
        printf("Sent data with return code %d\n\n\n",rc);

        if(rc >= 0 && still_sampling == true) {
            app_watchdog_tickle_kernel();
            still_sampling = false;
        }

        //okay now try to get the time from the controller
        rc = signpost_timelocation_get_time(&utime);
        if(rc < 0) {
            printf("Failed to get time - assuming 10 seconds\n");
            utime += 10;
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

    //initialize the signpost API
    int rc;
    do {
        rc = signpost_init("lab11/audio");
        if (rc < 0) {
            printf(" - Error initializing bus (code %d). Sleeping for 5s\n",rc);
            delay_ms(5000);
        }
    } while (rc < 0);
    printf(" * Bus initialized\n");

    gpio_enable_output(8);
    gpio_enable_output(9);
    gpio_clear(8);
    gpio_clear(9);

    send_buf[0] = 0x03;

    gpio_enable_output(STROBE);
    gpio_enable_output(RESET);
    gpio_enable_output(POWER);

    gpio_clear(POWER);
    gpio_clear(STROBE);
    gpio_clear(RESET);

    // start up the app watchdog
    printf("Starting watchdog\n");
    app_watchdog_set_kernel_timeout(60000);
    app_watchdog_start();

    //init adc
    adc_set_callback(adc_callback, NULL);
    adc_continuous_sample(0,700);

    //start timer
    printf("Starting timer\n");
    static tock_timer_t send_timer;
    timer_every(1000, timer_callback, NULL, &send_timer);
}
