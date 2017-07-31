
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
//#include <math.h>

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

#define ADC_CHANNEL 2
#define BUF_SIZE 3000
uint16_t buffer1[BUF_SIZE];
uint16_t buffer2[BUF_SIZE];

static void adc_callback (
        uint8_t channel __attribute__ ((unused)),
        uint32_t number_of_samples __attribute__ ((unused)),
        uint16_t* buffer,
        void* ud __attribute__ ((unused))
        ) {

    int av = 0;
    int im = 0;
    for(int i = 0; i < BUF_SIZE; i++) {
        im = buffer[i] - 2048;
        if(im < 0) im = im*-1;
        av += im;
    }

    printf("%d\n",av/BUF_SIZE);
}

int main (void) {
    printf("Starting App\n");

    adc_set_buffer(buffer1, BUF_SIZE);
    adc_set_double_buffer(buffer2, BUF_SIZE);

    adc_set_continuous_buffered_sample_callback(adc_callback, NULL);

    adc_continuous_buffered_sample(ADC_CHANNEL, 44100);
}
