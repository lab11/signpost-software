#pragma once

#include "tock.h"

#ifdef __cplusplus
extern "C" {
#endif

// Set a callback for this driver.
int i2c_selector_set_callback(int selector_num, subscribe_cb callback, void* callback_args);

// Channels is a bit mask, where a "1" means activate that I2C channel.
// The uint32_t is bit-packed such that the first selector is the lowest
// four bits, the next is the next four bits, etc.
int i2c_selector_select_channels(int selector_num, uint32_t channels);

// Disable all channels on all I2C selectors.
int i2c_selector_disable_all_channels(int selector_num);

// Read interrupts on all I2C selectors
int i2c_selector_read_interrupts(int selector_num);

int i2c_selector_read_selected(int selector_num);


// Synchronous versions
//The synchronous versions will also return and set all selectors
int i2c_selector_select_channels_sync(uint32_t channels);
int i2c_selector_disable_all_channels_sync(void);
int i2c_selector_read_interrupts_sync(void);
int i2c_selector_read_selected_sync(void);

#ifdef __cplusplus
}
#endif
