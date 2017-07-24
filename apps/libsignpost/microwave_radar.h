#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef void (sample_cb)(uint16_t);
#define MR_ADC_CHANNEL 0

//initializes microwave radar and start sampling
int mr_init(void);

//boolean is motion or not. compares motion index to threshold
bool mr_is_motion(void);

//get the raw motion index. This is in made up units
//Really its the sliding window integral of the ADC signal
uint32_t mr_motion_index(void);

//Tries to calculate the frequency of the motion signal
//This is a very very rough approximation of speed
uint32_t mr_motion_frequency_hz(void);

//converts frequency in hz to milli-m/s
uint32_t mr_frequency_to_speed_mmps(uint32_t frequency);

//a way for the higher layer app to get raw adc samples
int mr_subscribe_samples(sample_cb cb);

//returns the internal sampling frequency
uint32_t mr_get_sample_frequency_hz(void);

//returns the threshold at which the library detects motions
uint32_t mr_get_motion_threshold(void);
