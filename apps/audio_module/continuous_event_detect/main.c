#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#include <adc.h>
#include <alarm.h>
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

#define EVENT_AUDIO_VERSION 1
#define EVENT_AUDIO_LEN 5

#define AUDIO_RIDGE_VERSION 1
#define AUDIO_RIDGE_HEADER_LEN 6
#define AUDIO_RIDGE_FRAGMENT_LEN 64
#define AUDIO_RIDGE_LEN (AUDIO_RIDGE_HEADER_LEN + AUDIO_RIDGE_FRAGMENT_LEN)


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
static void report_event_occurred (uint32_t event_id, uint8_t* report_data, uint16_t report_len);

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

    // send detailed data once per minute
    if (event_count == 1) {
      // stop the ADC briefly and send messages
      stop_sampling();
      printf("Event detected!\n");
      // the current alarm ticks seems like a reasonable enough way to disambiguate events
      uint32_t event_id = alarm_read();
      report_event_occurred(event_id, (uint8_t*)snr_out, sizeof(kiss_fft_scalar)*FFT_FRAME_LEN);
      ridgeTracker_reset();

      // restarting the ADC will mean that we are on the first buffer again
      is_first_buffer = true;
      start_sampling();
    }
  }

  // check to see if we should report information
  iterations++;
  if (iterations % 150 == 0) {
    printf("itr: %d\n", iterations);
  }
  if (iterations * MS_PER_ITERATION > REPORT_EVENTS_INTERVAL_MS) {
    iterations = 0;

    // stop the ADC briefly and send messages
    stop_sampling();
    printf("Sending event report! - %lu events\n", event_count);
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

  // put data into buffer
  uint8_t data_buf[EVENT_AUDIO_LEN];
  data_buf[0] = EVENT_AUDIO_VERSION;
  data_buf[1] = (uint8_t)((event_count >> 24) & 0xFF);
  data_buf[2] = (uint8_t)((event_count >> 16) & 0xFF);
  data_buf[3] = (uint8_t)((event_count >>  8) & 0xFF);
  data_buf[4] = (uint8_t)((event_count      ) & 0xFF);

  int err = signpost_networking_send("lab11/eventAudio", data_buf, EVENT_AUDIO_LEN);
  if (err >= SB_PORT_SUCCESS) {
    // tickle watchdog
    app_watchdog_tickle_kernel();
  } else {
    printf("Networking send failed: %d\n", err);
  }
}

// asynchronously report an event triggering through the eventual interface
static void report_event_occurred (uint32_t event_id, uint8_t* report_data, uint16_t report_len) {

  // put data into buffer
  uint8_t data_buf[AUDIO_RIDGE_LEN];
  data_buf[0] = AUDIO_RIDGE_VERSION;
  data_buf[1] = (uint8_t)((event_id >> 24) & 0xFF);
  data_buf[2] = (uint8_t)((event_id >> 16) & 0xFF);
  data_buf[3] = (uint8_t)((event_id >>  8) & 0xFF);
  data_buf[4] = (uint8_t)((event_id      ) & 0xFF);
  data_buf[5] = 0; // packet fragment number

  // send each report fragment
  uint16_t bytes_remaining = report_len;
  uint8_t fragment_index = 0;
  while (bytes_remaining > 0) {

    // record fragment number
    data_buf[5] = fragment_index;

    // copy bytes into data buffer
    uint8_t bytes_written = AUDIO_RIDGE_FRAGMENT_LEN;
    if (bytes_written > bytes_remaining) {
      bytes_written = bytes_remaining;
    }
    memcpy(&(data_buf[6]), &(report_data[fragment_index * AUDIO_RIDGE_FRAGMENT_LEN]), bytes_written);

    // send data to radio
    uint8_t data_buf_len = AUDIO_RIDGE_HEADER_LEN + bytes_written;
    int err = signpost_networking_send_eventually("lab11_audioRidge", data_buf, data_buf_len);
    if (err >= SB_PORT_SUCCESS) {
      // tickle watchdog
      app_watchdog_tickle_kernel();
    } else {
      printf("Networking send eventually failed: %d\n", err);
    }

    // wait for the storage master
    delay_ms(500);

    // update state
    bytes_remaining -= bytes_written;
    fragment_index++;
  }
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
  // warning: there may be a bug with the timeout period. Setting it rather
  // long here in order to compensate
  app_watchdog_set_kernel_timeout(15*60*1000);
  app_watchdog_start();
  printf(" * Watchdog enabled\n");

  // start up audio processing chain
  audio_initialize();

  // return successfully. The system automatically calls `yield` continuously
  // for us
  return 0;
}

