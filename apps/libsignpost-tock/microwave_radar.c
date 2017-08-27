#include <stdint.h>
#include <stdbool.h>
#include <tock.h>
#include <adc.h>
#include "microwave_radar.h"

//Sample frequency of 4khz because if you look at the raw signal you don't
//see signal features >2khz
#define SAMPLE_FREQ 4000

//This was found moving from about 10m away and observing the output of the
//algorithm
#define MOTION_THRESHOLD 1000000
static sample_cb* sample_callback = NULL;

//these are the structures which are going to store the motion index
#define ACCUMULATOR_SIZE 16
static uint32_t accumulator_buffer[ACCUMULATOR_SIZE];
static uint8_t accumulator_pointer = 0;
#define COUNTER_SIZE 256
static uint32_t accumulator_counter = 0;
static uint32_t mr_index = 0;
static uint32_t motion_frequency = 0;

#define RISING 0
#define FALLING 1
#define FINISHED_RISING 2
#define FINISHED_FALLING 3
#define NOISE_OFFSET 100

static void motion_frequency_calculator(uint16_t sample) {

    static int sample_state = RISING;
    static uint32_t sample_counter = 0;
    static uint32_t max_sample = 2024;
    static uint32_t min_sample = 2024;

    // keep sampling until signals peaks or bottoms
    if (sample_state == RISING) {
        if (sample <= (max_sample - NOISE_OFFSET)) {
            // data falling again
            sample_state = FINISHED_RISING;
        } else {
            if (sample > max_sample) {
                max_sample = sample;
            }
            sample_state = RISING;
            sample_counter++;
        }
    } else if (sample_state == FALLING) {
        if (sample >= (min_sample + NOISE_OFFSET)) {
            // data rising again
            sample_state = FINISHED_FALLING;
        } else {
            if (sample < min_sample) {
                min_sample = sample;
            }
            sample_state = FALLING;
            sample_counter++;
        }
    }

    // calculate time for that period
    if (sample_state == FINISHED_RISING || sample_state == FINISHED_FALLING) {

        motion_frequency = (uint32_t)(1.0/(sample_counter/(float)SAMPLE_FREQ));
        sample_counter = 0;

        if (sample_state == FINISHED_FALLING) {
            sample_state = RISING;
            max_sample = sample;
            min_sample = sample;
        } else if (sample_state == FINISHED_RISING) {
            sample_state = FALLING;
            min_sample = sample;
            max_sample = sample;
        }
    }
}

//the theory here is to do a rolling window integration of the samples aboslute
//value difference from no motion(3.3V/2)
//
//to save memory we make window bins
//
//the bin size is COUNTER_SIZE/SAMPLE_FREQ
//and the total window size is ACCUMULATOR_SIZE*COUNTER_SIZE/SAMPLE_FREQ
static void adc_callback(__attribute__((unused)) uint8_t channel,
                        uint16_t sample,
                        __attribute__((unused)) void * ud) {

    //add the sample to the accumulator
    int s = sample;
    s = s-2024;
    if(s < 0) {
        s = s * -1;
    }

    accumulator_buffer[accumulator_pointer] += s;

    //increment accumulator counter
    accumulator_counter++;

    //are we done with this slot?
    if(accumulator_counter == COUNTER_SIZE) {
        //update the mr_index
        mr_index = 0;
        for(uint8_t i = 0; i < ACCUMULATOR_SIZE; i++) {
            mr_index += accumulator_buffer[i];
        }

        //increment the pointer
        accumulator_pointer++;
        if(accumulator_pointer == ACCUMULATOR_SIZE) {
            accumulator_pointer = 0;
        }

        accumulator_buffer[accumulator_pointer] = 0;
        accumulator_counter = 0;

    }

    motion_frequency_calculator(sample);

    //call the sample callback if the app requested it
    if(sample_callback) {
        sample_callback(sample);
    }
}


//initializes microwave radar and start sampling
int mr_init(void) {
    //start ADC sampling at 4khz
    int ret = adc_set_continuous_sample_callback(adc_callback, NULL);
    ret |= adc_continuous_sample(MR_ADC_CHANNEL, SAMPLE_FREQ);

    return ret;
}

//this functionality isn't present in hardware yet
int mr_disable(void) {
    return TOCK_ENOSUPPORT;
}

int mr_enable(void) {
    return TOCK_ENOSUPPORT;
}

//boolean is motion or not. compares motion index to threshold
bool mr_is_motion(void) {
    return (mr_index > MOTION_THRESHOLD);
}

//get the raw motion index. This is in made up units
//Really its the sliding window integral of the ADC signal
uint32_t mr_motion_index(void) {
    return mr_index;
}

//Tries to calculate the frequency of the motion signal
//This is a very very rough approximation of speed
uint32_t mr_motion_frequency_hz(void) {
    return motion_frequency;
}

//converts frequency in hz to milli-m/s
uint32_t mr_frequency_to_speed_mmps(uint32_t frequency) {
    // Note: this speed is not really meaningful because it calculates
    // the corresponding speed when object moving perpendicular to sensor
    uint32_t speed_fph = (frequency * 5280)/31;
    uint32_t speed_mfps = (speed_fph*1000)/3600;
    uint32_t speed_mmps = (speed_mfps*1000)/3280;

    return speed_mmps;
}

//a way for the higher layer app to get raw adc samples
int mr_subscribe_samples(sample_cb cb) {
    sample_callback = cb;
    return 0;
}

//returns the internal sampling frequency
uint32_t mr_get_sample_frequency_hz(void) {
    return SAMPLE_FREQ;
}

//returns the threshold at which the library detects motions
uint32_t mr_get_motion_threshold(void) {
    return MOTION_THRESHOLD;
}
