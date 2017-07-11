
#include "tock.h"
#include "i2c_selector.h"

#define DRIVER_NUM_I2C_SELECTOR_0 1001
#define DRIVER_NUM_I2C_SELECTOR_1 1002
#define DRIVER_NUM_I2C_SELECTOR_2 1003


struct i2c_selector_data {
  bool fired;
  int value;
};

static struct i2c_selector_data result = { .fired = false };

// Internal callback for faking synchronous reads
static void i2c_selector_cb(__attribute__ ((unused)) int value,
                       __attribute__ ((unused)) int unused1,
                       __attribute__ ((unused)) int unused2,
                       void* ud) {
  struct i2c_selector_data* data = (struct i2c_selector_data*) ud;
  data->value = value;
  data->fired = true;
}


int i2c_selector_set_callback(subscribe_cb callback, void* callback_args) {
    int ret = subscribe(DRIVER_NUM_I2C_SELECTOR_0, 0, callback, callback_args);
    ret |=    subscribe(DRIVER_NUM_I2C_SELECTOR_1, 0, callback, callback_args);
    ret |=    subscribe(DRIVER_NUM_I2C_SELECTOR_2, 0, callback, callback_args);
    return ret;
}

int i2c_selector_select_channels(uint32_t channels) {
	int ret = command(DRIVER_NUM_I2C_SELECTOR_0, 1, channels & 0x0F);
	ret |=    command(DRIVER_NUM_I2C_SELECTOR_1, 1, (channels >> 4) & 0x0F);
	ret |=    command(DRIVER_NUM_I2C_SELECTOR_1, 1, (channels >> 8) & 0x0F);
    return ret;
}

int i2c_selector_disable_all_channels(void) {
	int ret = command(DRIVER_NUM_I2C_SELECTOR_0, 2, 0);
	ret |= command(DRIVER_NUM_I2C_SELECTOR_1, 2, 0);
    ret |= command(DRIVER_NUM_I2C_SELECTOR_2, 2, 0);
    return ret;
}

int i2c_selector_read_interrupts(void) {
	int ret =  command(DRIVER_NUM_I2C_SELECTOR_0, 3, 0);
    ret |= command(DRIVER_NUM_I2C_SELECTOR_1, 3, 0);
	ret |= command(DRIVER_NUM_I2C_SELECTOR_2, 3, 0);
    return ret;
}

int i2c_selector_read_selected(void) {
	int ret = command(DRIVER_NUM_I2C_SELECTOR_0, 4, 0);
	ret |= command(DRIVER_NUM_I2C_SELECTOR_1, 4, 0);
	ret |= command(DRIVER_NUM_I2C_SELECTOR_2, 4, 0);
    return ret;
}

int i2c_selector_select_channels_sync(uint32_t channels) {
    int err;
    result.fired = false;

    err = i2c_selector_set_callback(i2c_selector_cb, (void*) &result);
    if (err < 0) return err;

    err = i2c_selector_select_channels(channels);
    if (err < 0) return err;

    // Wait for the callback.
    yield_for(&result.fired);

    return 0;
}

int i2c_selector_disable_all_channels_sync(void) {
    int err;
    result.fired = false;

    err = i2c_selector_set_callback(i2c_selector_cb, (void*) &result);
    if (err < 0) return err;

    err = i2c_selector_disable_all_channels();
    if (err < 0) return err;

    // Wait for the callback.
    yield_for(&result.fired);

    return 0;
}

int i2c_selector_read_interrupts_sync(void) {
    int err;
    result.fired = false;

    err = i2c_selector_set_callback(i2c_selector_cb, (void*) &result);
    if (err < 0) return err;

    err = i2c_selector_read_interrupts();
    if (err < 0) return err;

    // Wait for the callback.
    yield_for(&result.fired);

    return result.value;
}

int i2c_selector_read_selected_sync(void) {
    int err;
    result.fired = false;

    err = i2c_selector_set_callback(i2c_selector_cb, (void*) &result);
    if (err < 0) return err;

    err = i2c_selector_read_selected();
    if (err < 0) return err;

    // Wait for the callback.
    yield_for(&result.fired);

    return result.value;
}
