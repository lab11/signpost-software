#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#include <adc.h>
#include <console.h>
#include <gpio.h>
#include <led.h>
#include <timer.h>
#include <tock.h>

#include "_kiss_fft_guts.h"
#include "kiss_fftr.h"
#include "ridgeTracker.h"
#include "common.h"

#define AUDIO_ADC_CHANNEL 2
#define AUDIO_SAMPLE_FREQUENCY 16000
#define AUDIO_FRAME_LEN 256
#define ANALYSIS_FRAME_LEN 256
#define FFT_FRAME_LEN 2*AUDIO_FRAME_LEN

#define BUFFER_LEN 2*AUDIO_FRAME_LEN
static uint16_t sample_buffer[BUFFER_LEN];

static uint8_t buffer_offset = 0;
#define MAX_BUFFER_OFFSET (BUFFER_LEN / AUDIO_FRAME_LEN)

extern const kiss_fft_scalar WINDOW[FFT_FRAME_LEN];

// okay, so this is the actual size malloc'd in `kiss_fftr_alloc`, which we
// need to allocate statically and copy into when copying the
// kiss_configuration before running an FFT
#define KISS_FFTR_STATE_SIZE (3*sizeof(int*))
#define KISS_CONFIGURATION_SIZE (KISS_FFTR_STATE_SIZE + sizeof(kiss_fft_cpx) * (FFT_FRAME_LEN/2 * 3 / 2) + (sizeof(struct kiss_fft_state) + sizeof(kiss_fft_cpx) * (FFT_FRAME_LEN/2 - 1)))
static kiss_fftr_cfg kiss_configuration;

// function prototypes
static void analyze_frequency(kiss_fft_scalar* frame, kiss_fft_scalar* spectrum);


static void continuous_buffered_sample_cb(uint8_t channel,
                                          uint32_t length,
                                          uint16_t* buf_ptr,
                                          __attribute__ ((unused)) void* callback_args) {
  // keep track of which buffer to update
  static bool is_first_buffer = true;

  // local buffer for analyzed data
  static kiss_fft_scalar fft_frame[FFT_FRAME_LEN];
  static kiss_fft_scalar spectrum[ANALYSIS_FRAME_LEN];
  static kiss_fft_scalar snr_out[ANALYSIS_FRAME_LEN]; //XXX: is this even necessary?

  //XXX: toggle debug pin
  led_on(0);

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

  // copy last two audio frames into buffer for analysis
  uint16_t* prev_buf_ptr = &(sample_buffer[(MAX_BUFFER_OFFSET-1)*AUDIO_FRAME_LEN]);
  if (buffer_offset > 0) {
    prev_buf_ptr = &(sample_buffer[(buffer_offset-1)*AUDIO_FRAME_LEN]);
  }
  memcpy(&(fft_frame[0]), prev_buf_ptr, AUDIO_FRAME_LEN);
  memcpy(&(fft_frame[AUDIO_FRAME_LEN]), buf_ptr, AUDIO_FRAME_LEN);

  // perfrom FFT
  analyze_frequency(fft_frame, spectrum);

  // update ridge-tracking algorithm
  ridgeTracker_update(spectrum, snr_out);

  // check for event
  if (ridgeTracker_isReady) {
    printf("Event detected!\n");

    ridgeTracker_reset();
  }

  // update buffer offset
  buffer_offset++;
  if (buffer_offset >= MAX_BUFFER_OFFSET) {
    buffer_offset = 0;
  }

  // next callback will be the other buffer
  is_first_buffer = !is_first_buffer;

  //XXX: toggle debug pin
  led_off(0);
}


static void analyze_frequency(kiss_fft_scalar* frame, kiss_fft_scalar* spectrum) {
  // FFT data structure
  static kiss_fft_scalar in[FFT_FRAME_LEN];
  static kiss_fft_cpx out[ANALYSIS_FRAME_LEN + 1];
  static uint8_t fft_cfg_bytes[KISS_CONFIGURATION_SIZE];
  //static kiss_fftr_cfg cfg = (kiss_fftr_cfg)fft_cfg_bytes;

  // windowing and prescaling
  for (size_t i = 0; i < FFT_FRAME_LEN; i++){
    in[i] = S_MUL(frame[i], WINDOW[i]) << 4;
  }

  // use default configuration for FFT
  kiss_fftr_cfg cfg = kiss_configuration;

  // run the FFTs
  kiss_fftr(cfg, in, out);

  // save the magnitude of the data
  for (size_t k = 0; k < ANALYSIS_FRAME_LEN; k++){
    spectrum[k] = MAG(out[k].r, out[k].i);
  }
}


int main (void) {
  int err;
  printf("[Audio Module] Continuous ADC sampling\n");

  // checking invariants
  assert(AUDIO_FRAME_LEN == INC_LEN);
  assert(AUDIO_FRAME_LEN == FRE_LEN);
  assert(AUDIO_FRAME_LEN == ANALYSIS_FRAME_LEN);
  assert(FFT_FRAME_LEN == BUF_LEN);

  // initialize ridge-tracking library
  ridgeTracker_init();

  // generate a kiss fft configuration which we need for running FFTs. This
  // takes a long time to generate (100-200 ms) but is the same every time its
  // made, so we generate it once here and perform deep copies of the original
  // whenever we need to run an FFT.
  kiss_configuration = kiss_fftr_alloc(FFT_FRAME_LEN, 0, NULL, NULL);
  if (kiss_configuration == NULL) {
    printf("Kiss FFT failed to allocate configuration memory\n");
    return -1;
  }
  //XXX: this configuration could be moved into ROM. But if we do so, we need
  //to make sure to include an updated pointer to tmpbuf
  //Probably best way to do it is to put substate and super_twiddles in ROM separately
  // and then craft a kiss_configuration that points to them and a tmpbuf in RAM

  // test that we correctly determined the kiss configuration size
  size_t realsize = 0;
  kiss_fftr_alloc(FFT_FRAME_LEN, 0, NULL, &realsize);
  assert(realsize == KISS_CONFIGURATION_SIZE);

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
}

