#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

#include <adc.h>
#include <console.h>
#include <gpio.h>
#include <led.h>
#include <timer.h>
#include <tock.h>

#define AUDIO_ADC_CHANNEL 2
#define AUDIO_SAMPLE_FREQUENCY 16000
#define AUDIO_FRAME_LEN 255

// 25*2*256 = 12800 samples = 800 milliseconds
#define BUFFER_LEN 25*2*AUDIO_FRAME_LEN
static uint16_t sample_buffer[BUFFER_LEN];

static uint8_t buffer_offset = 0;
#define MAX_BUFFER_OFFSET (BUFFER_LEN / AUDIO_FRAME_LEN)


static void continuous_buffered_sample_cb(uint8_t channel,
                                          uint32_t length,
                                          uint16_t* buf_ptr,
                                          __attribute__ ((unused)) void* callback_args) {
  // keep track of which buffer to update
  static bool is_first_buffer = true;

  // local buffer for fft data
  kiss_fft_scalar fft_frame[2*AUDIO_FRAME_LEN];

  // update buffer pointer
  if (is_first_buffer) {

    // set main buffer for ADC samples
    int err = adc_set_buffer(&(sample_buffer[buffer_offset*AUDIO_FRAME_LEN]), AUDIO_FRAME_LEN);
    if (err < TOCK_SUCCESS) {
      printf("set buffer error: %d - %s\n", err, tock_strerror(err));
    }
  } else {

    // set secondary buffer for ADC samples
    int err = adc_set_double_buffer(&(sample_buffer[buffer_offset*AUDIO_FRAME_LEN]), AUDIO_FRAME_LEN);
    if (err < TOCK_SUCCESS) {
      printf("set double buffer error: %d - %s\n", err, tock_strerror(err));
    }
  }

  // copy into scratch buffer
  prev_buf_ptr = &(sample_buffer[(MAX_BUFFER_OFFSET-1)*AUDIO_FRAME_LEN]);
  if (buffer_offset > 0) {
    prev_buf_ptr = &(sample_buffer[(buffer_offset-1)*AUDIO_FRAME_LEN]);
  }
  memcpy(&(fft_frame[0]), prev_buf_ptr, AUDIO_FRAME_LEN);
  memcpy(&(fft_frame[AUDIO_FRAME_LEN]), buf_ptr, AUDIO_FRAME_LEN);

  // look for events in the audio buffer

  // update buffer offset
  buffer_offset++;
  if (buffer_offset >= MAX_BUFFER_OFFSET) {
    buffer_offset = 0;
  }

  // next callback will be the other buffer
  is_first_buffer = !is_first_buffer;
}


int main (void) {
  int err;
  printf("[Audio Module] Continuous ADC sampling\n");

  // print out possible buffer addresses
  printf("Buffer locations:\n");
  for (int i=0; i<25*2; i++) {
    printf("\t%s%d - 0x%X\n", ((i<10)?" ":""), i, &(sample_buffer[i*AUDIO_FRAME_LEN]));
  }

  // set ADC callbacks
  err = adc_set_continuous_buffered_sample_callback(continuous_buffered_sample_cb, NULL);
  if (err < TOCK_SUCCESS) {
    printf("set continuous buffered sample callback error: %d\n", err);
    return -1;
  }

  // set main buffer for ADC samples
  err = adc_set_buffer(&(sample_buffer[buffer_offset*AUDIO_FRAME_LEN]), AUDIO_FRAME_LEN);
  if (err < TOCK_SUCCESS) {
    printf("set buffer error: %d - %s\n", err, tock_strerror(err));
    return -1;
  }
  buffer_offset++;

  // set secondary buffer for ADC samples. In continuous mode, the ADC will
  // automatically switch between the two each callback
  err = adc_set_double_buffer(&(sample_buffer[buffer_offset*AUDIO_FRAME_LEN]), AUDIO_FRAME_LEN);
  if (err < TOCK_SUCCESS) {
    printf("set double buffer error: %d - %s\n", err, tock_strerror(err));
    return -1;
  }
  buffer_offset++;

  // begin continuous sampling
  printf("Beginning continuous sampling on channel %d at %d Hz\n",
         AUDIO_ADC_CHANNEL, AUDIO_SAMPLE_FREQUENCY);
  err = adc_continuous_buffered_sample(AUDIO_ADC_CHANNEL, AUDIO_SAMPLE_FREQUENCY);
  if (err < TOCK_SUCCESS) {
    printf("continuous sample error: %d - %s\n", err, tock_strerror(err));
    return -1;
  }

  // return successfully. The system automatically calls `yield` continuously
  // for us
  return 0;


  /*
  while (true) {
    printf("Sampling in 3..\n");
    delay_ms(1000);
    printf("            2..\n");
    delay_ms(1000);
    printf("            1..\n");
    delay_ms(1000);

    int err = adc_sample_buffer_sync(MEMS_ADC_CHANNEL, AUDIO_SAMPLE_FREQ, sample_buffer, BUFFER_LEN);
    if (err < 0) {
      printf("Error sampling ADC: %d\n", err);
    } else {
      printf("\t[ ");
      for (uint32_t i = 0; i < BUFFER_LEN; i++) {
        // convert to millivolts
        printf("%u ", (sample_buffer[i] * 3300) / 4095);
        if (i%10 == 0) {
          printf("\n");
        }
      }
      printf("]\n");
    }
  }
  */
}

