#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <adc.h>
#include <console.h>
#include <gpio.h>
#include <led.h>
#include <timer.h>
#include <tock.h>

int main (void) {
  int err = TOCK_SUCCESS;
  printf("[Audio Module] Simple ADC test\n");

  printf("Sampling data\n");
  uint8_t sample_index = 0;
  while (true) {

    // read data from ADC
    uint16_t sample;
    err = adc_sample_sync(3,&sample);
    if (err < TOCK_SUCCESS) {
      printf("ADC read error: %d\n", err);
    }
    printf("%d\n", sample);

    delay_ms(1);
  }
}

