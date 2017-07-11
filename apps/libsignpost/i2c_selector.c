
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


int i2c_selector_set_callback(int selector_num, subscribe_cb callback, void* callback_args) {
    if(selector_num == 0) {
        return subscribe(DRIVER_NUM_I2C_SELECTOR_0, 0, callback, callback_args);
    } else if (selector_num == 1) {
        return subscribe(DRIVER_NUM_I2C_SELECTOR_1, 0, callback, callback_args);
    } else if (selector_num == 2) {
        return subscribe(DRIVER_NUM_I2C_SELECTOR_2, 0, callback, callback_args);
    } else {
        return TOCK_ENODEVICE;
    }
}

int i2c_selector_select_channels(int selector_num, uint32_t channels) {
    if(selector_num == 0) {
	    return command(DRIVER_NUM_I2C_SELECTOR_0, 1, channels & 0x0000000F);
    } else if (selector_num == 1) {
	    return command(DRIVER_NUM_I2C_SELECTOR_1, 1, channels & 0x0000000F);
    } else if (selector_num == 2) {
	    return command(DRIVER_NUM_I2C_SELECTOR_2, 1, channels & 0x0000000F);
    } else {
        return TOCK_ENODEVICE;
    }
}

int i2c_selector_disable_all_channels(int selector_num) {
    if(selector_num == 0) {
        return command(DRIVER_NUM_I2C_SELECTOR_0, 2, 0);
    } else if (selector_num == 1) {
        return command(DRIVER_NUM_I2C_SELECTOR_1, 2, 0);
    } else if (selector_num == 2) {
        return command(DRIVER_NUM_I2C_SELECTOR_2, 2, 0);
    } else {
        return TOCK_ENODEVICE;
    }
}

int i2c_selector_read_interrupts(int selector_num) {
    if(selector_num == 0) {
        return command(DRIVER_NUM_I2C_SELECTOR_0, 3, 0);
    } else if (selector_num == 1) {
        return command(DRIVER_NUM_I2C_SELECTOR_1, 3, 0);
    } else if (selector_num == 2) {
        return command(DRIVER_NUM_I2C_SELECTOR_2, 3, 0);
    } else {
        return TOCK_ENODEVICE;
    }
}

int i2c_selector_read_selected(int selector_num) {
    if(selector_num == 0) {
        return command(DRIVER_NUM_I2C_SELECTOR_0, 3, 0);
    } else if (selector_num == 1) {
        return command(DRIVER_NUM_I2C_SELECTOR_1, 3, 0);
    } else if (selector_num == 2) {
        return command(DRIVER_NUM_I2C_SELECTOR_2, 3, 0);
    } else {
        return TOCK_ENODEVICE;
    }
}


int i2c_selector_select_channels_sync(uint32_t channels) {
    int err;
    result.fired = false;

    err = i2c_selector_set_callback(0,i2c_selector_cb, (void*) &result);
    if (err < 0) return err;
    err = i2c_selector_set_callback(1,i2c_selector_cb, (void*) &result);
    if (err < 0) return err;
    err = i2c_selector_set_callback(2,i2c_selector_cb, (void*) &result);
    if (err < 0) return err;

    err = i2c_selector_select_channels(0,channels);
    if (err < 0) return err;

    // Wait for the callback.
    yield_for(&result.fired);

    result.fired = false;
    err = i2c_selector_select_channels(1,channels>>4);
    if (err < 0) return err;

    // Wait for the callback.
    yield_for(&result.fired);

    result.fired = false;
    err = i2c_selector_select_channels(2,channels>>8);
    if (err < 0) return err;

    // Wait for the callback.
    yield_for(&result.fired);


    return 0;
}

int i2c_selector_disable_all_channels_sync(void) {
    int err;
    result.fired = false;

    err = i2c_selector_set_callback(0,i2c_selector_cb, (void*) &result);
    if (err < 0) return err;
    err = i2c_selector_set_callback(1,i2c_selector_cb, (void*) &result);
    if (err < 0) return err;
    err = i2c_selector_set_callback(2,i2c_selector_cb, (void*) &result);
    if (err < 0) return err;

    err = i2c_selector_disable_all_channels(0);
    if (err < 0) return err;

    // Wait for the callback.
    yield_for(&result.fired);

    result.fired = false;
    err = i2c_selector_disable_all_channels(1);
    if (err < 0) return err;

    // Wait for the callback.
    yield_for(&result.fired);

    result.fired = false;
    err = i2c_selector_disable_all_channels(2);
    if (err < 0) return err;

    // Wait for the callback.
    yield_for(&result.fired);

    return 0;
}

int i2c_selector_read_interrupts_sync(void) {
    int err;
    result.fired = false;

    int value;

    err = i2c_selector_set_callback(0,i2c_selector_cb, (void*) &result);
    if (err < 0) return err;
    err = i2c_selector_set_callback(1,i2c_selector_cb, (void*) &result);
    if (err < 0) return err;
    err = i2c_selector_set_callback(2,i2c_selector_cb, (void*) &result);
    if (err < 0) return err;

    err = i2c_selector_read_interrupts(0);
    if (err < 0) return err;

    // Wait for the callback.
    yield_for(&result.fired);

    value = result.value;

    result.fired = false;
    err = i2c_selector_read_interrupts(1);
    if (err < 0) return err;

    // Wait for the callback.
    yield_for(&result.fired);

    value |= result.value << 4;

    result.fired = false;
    err = i2c_selector_read_interrupts(2);
    if (err < 0) return err;

    // Wait for the callback.
    yield_for(&result.fired);

    value |= result.value << 8;

    return value;
}

int i2c_selector_read_selected_sync(void) {

    int err;
    result.fired = false;

    int value;

    err = i2c_selector_set_callback(0,i2c_selector_cb, (void*) &result);
    if (err < 0) return err;
    err = i2c_selector_set_callback(1,i2c_selector_cb, (void*) &result);
    if (err < 0) return err;
    err = i2c_selector_set_callback(2,i2c_selector_cb, (void*) &result);
    if (err < 0) return err;

    err = i2c_selector_read_selected(0);
    if (err < 0) return err;

    // Wait for the callback.
    yield_for(&result.fired);

    value = result.value;

    result.fired = false;
    err = i2c_selector_read_selected(1);
    if (err < 0) return err;

    // Wait for the callback.
    yield_for(&result.fired);

    value |= result.value << 4;

    result.fired = false;
    err = i2c_selector_read_selected(2);
    if (err < 0) return err;

    // Wait for the callback.
    yield_for(&result.fired);

    value |= result.value << 8;

    return value;
}
