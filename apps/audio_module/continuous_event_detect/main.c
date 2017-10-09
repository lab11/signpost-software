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

#include "app_watchdog.h"
#include "port_signpost.h"
#include "signpost_api.h"

#include "_kiss_fft_guts.h"
#include "kiss_fftr.h"
#include "ridgeTracker.h"
#include "common.h"

#define AUDIO_ADC_CHANNEL 2
#define AUDIO_SAMPLE_FREQUENCY 16000
#define AUDIO_FRAME_LEN 256
#define ANALYSIS_FRAME_LEN 256
#define FFT_FRAME_LEN 2*AUDIO_FRAME_LEN
#define MS_PER_ITERATION 16
#define REPORT_EVENTS_INTERVAL_MS 60*1000

#define BUFFER_LEN 2*AUDIO_FRAME_LEN
static uint16_t sample_buffer[BUFFER_LEN];

static uint8_t buffer_offset = 0;
#define MAX_BUFFER_OFFSET (BUFFER_LEN / AUDIO_FRAME_LEN)

// FFT parameters stored in FLASH
extern const kiss_fft_scalar WINDOW[FFT_FRAME_LEN];
extern const uint8_t KISS_FFT_CFG_SUBSTATE[1288];
extern const uint8_t KISS_FFT_CPX_SUPER_TWIDDLES[512];

// configuration for kiss fft library
static uint8_t* kiss_configuration[3];
static kiss_fft_cpx kiss_config_tmpbuf[FFT_FRAME_LEN/2];

// function prototypes
static void analyze_frequency(kiss_fft_scalar* frame, kiss_fft_scalar* spectrum);
static int audio_initialize(void);
static int start_sampling(void);
static int stop_sampling(void);
static void report_events(uint32_t event_count);
static void report_event_occurred(void);

// get callback when a buffer is filled with audio samples
// should be called every ~16 ms (16000/256 times per second)
static void continuous_buffered_sample_cb(__attribute__ ((unused)) uint8_t channel,
                                          __attribute__ ((unused)) uint32_t length,
                                          uint16_t* buf_ptr,
                                          __attribute__ ((unused)) void* callback_args) {
  // keep track of which buffer to update
  static bool is_first_buffer = true;

  // keep track of iterations to report events periodically
  static uint16_t iterations = 0;

  // keep track of how many events have been detected
  static uint32_t event_count = 0;

  // local buffer for analyzed data
  static kiss_fft_scalar fft_frame[FFT_FRAME_LEN];
  static kiss_fft_scalar spectrum[ANALYSIS_FRAME_LEN];

  // we'll just put snr results in the first half of the fft frame to save
  // memory instead of putting it separately in the data section
  //static kiss_fft_scalar snr_out[ANALYSIS_FRAME_LEN];
  kiss_fft_scalar* snr_out = fft_frame;

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

  // next callback will be the other buffer
  is_first_buffer = !is_first_buffer;

  // copy last two audio frames into buffer for analysis
  uint16_t* prev_buf_ptr = &(sample_buffer[(MAX_BUFFER_OFFSET-1)*AUDIO_FRAME_LEN]);
  if (buffer_offset > 0) {
    prev_buf_ptr = &(sample_buffer[(buffer_offset-1)*AUDIO_FRAME_LEN]);
  }
  memcpy(&(fft_frame[0]), prev_buf_ptr, AUDIO_FRAME_LEN);
  memcpy(&(fft_frame[AUDIO_FRAME_LEN]), buf_ptr, AUDIO_FRAME_LEN);

  // update buffer offset
  buffer_offset++;
  if (buffer_offset >= MAX_BUFFER_OFFSET) {
    buffer_offset = 0;
  }

  // perform FFT
  analyze_frequency(fft_frame, spectrum);

  // update ridge-tracking algorithm
  ridgeTracker_update(spectrum, snr_out);

  // check for event
  if (ridgeTracker_isReady) {
    event_count++;

    // stop the ADC briefly and send messages
    stop_sampling();
    printf("Event detected!\n");
    report_event_occurred();
    ridgeTracker_reset();

    // restarting the ADC will mean that we are on the first buffer again
    is_first_buffer = true;
    start_sampling();
  }

  // check to see if we should report information
  iterations++;
  if (iterations * MS_PER_ITERATION > REPORT_EVENTS_INTERVAL_MS) {
    iterations = 0;

    // stop the ADC briefly and send messages
    stop_sampling();
    printf("Sending event report!\n");
    report_events(event_count);
    event_count = 0;

    // restarting the ADC will mean that we are on the first buffer again
    is_first_buffer = true;
    start_sampling();
  }

  //XXX: toggle debug pin
  led_off(0);
}

static void analyze_frequency(kiss_fft_scalar* frame, kiss_fft_scalar* spectrum) {
  // FFT data structures
  kiss_fft_cpx out[ANALYSIS_FRAME_LEN + 1];

  // windowing and prescaling
  // modified to do so in-place
  for (size_t i = 0; i < FFT_FRAME_LEN; i++){
    frame[i] = S_MUL(frame[i], WINDOW[i]) << 4;
  }

  // use default configuration for FFT
  kiss_fftr_cfg cfg = (kiss_fftr_cfg)kiss_configuration;

  // run the FFTs
  kiss_fftr(cfg, frame, out);

  // save the magnitude of the data
  for (size_t k = 0; k < ANALYSIS_FRAME_LEN; k++){
    spectrum[k] = MAG(out[k].r, out[k].i);
  }
}

static int audio_initialize(void) {
  int err;

  // checking invariants
  assert(AUDIO_FRAME_LEN == INC_LEN);
  assert(AUDIO_FRAME_LEN == FRE_LEN);
  assert(AUDIO_FRAME_LEN == ANALYSIS_FRAME_LEN);
  assert(FFT_FRAME_LEN == BUF_LEN);

  // initialize ridge-tracking library
  ridgeTracker_init();

  // create a kiss configuration!
  // Instead of malloc-ing the whole thing on the heap, we've previously
  // generated a configuration and placed it into Flash (except tmpbuf, which
  // needs to be in RAM). So just build the kiss configuration out of those
  // parts
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdiscarded-qualifiers"
  kiss_configuration[0] = KISS_FFT_CFG_SUBSTATE;
  kiss_configuration[1] = (uint8_t*)kiss_config_tmpbuf;
  kiss_configuration[2] = KISS_FFT_CPX_SUPER_TWIDDLES;
#pragma GCC diagnostic pop

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

  // begin sampling the ADC
  err = start_sampling();
  if (err < TOCK_SUCCESS) {
    printf("continuous sample error: %d - %s\n", err, tock_strerror(err));
    return -1;
  }
  printf(" * Started continuous sampling on channel %d at %d Hz\n",
         AUDIO_ADC_CHANNEL, AUDIO_SAMPLE_FREQUENCY);

  return TOCK_SUCCESS;
}

static int start_sampling (void) {
  // begin continuous sampling
  return adc_continuous_buffered_sample(AUDIO_ADC_CHANNEL, AUDIO_SAMPLE_FREQUENCY);
}

static int stop_sampling (void) {
  return adc_stop_sampling();
}

// periodically report events over the radio
static void report_events (uint32_t event_count) {

  printf("Servicing timer callback!\n");
  delay_ms(2000 + event_count);
  printf("Done servicing\n");

  // tickle watchdog
  app_watchdog_tickle_kernel();
}

// asynchronously report an event triggering through the eventual interface
static void report_event_occurred (void) {

  // tickle watchdog
  app_watchdog_tickle_kernel();
}

int main (void) {
  printf("[Audio Module] Continuous ADC sampling\n");

  //initialize the signpost API
  int err;
  do {
    err = signpost_initialization_module_init(0x33, NULL);
    if (err < SB_PORT_SUCCESS) {
      printf(" - Error initializing bus (code %d). Sleeping for 5s\n", err);
      delay_ms(5000);
    }
  } while (err < SB_PORT_SUCCESS);
  printf(" * Bus initialized\n");

  // start up app watchdog
  app_watchdog_set_kernel_timeout(5*60*1000);
  app_watchdog_start();
  printf(" * Watchdog enabled\n");

  // start up audio processing chain
  audio_initialize();

  // return successfully. The system automatically calls `yield` continuously
  // for us
  return 0;
}

