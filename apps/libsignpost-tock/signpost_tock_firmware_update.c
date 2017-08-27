#include <stdbool.h>

#include "tock.h"
#include "signpost_tock_firmware_update.h"


int signpost_tock_firmware_update_go(uint32_t source,
                                     uint32_t destination,
                                     uint32_t length,
                                     uint32_t crc) {
  int ret;
  uint32_t config[4] = {source, destination, length, crc};

  ret = allow(DRIVER_NUM_STFU, 0, (uint8_t*) config, 16);
  if (ret < 0) return ret;

  return command(DRIVER_NUM_STFU, 1, 0);
}

struct stfu_holding_data {
  bool fired;
};

static struct stfu_holding_data result = { .fired = false };

// Internal callback for faking synchronous reads
static void stfu_holding_cb(__attribute__ ((unused)) int value,
                       __attribute__ ((unused)) int unused1,
                       __attribute__ ((unused)) int unused2,
                       void* ud) {
  struct stfu_holding_data* data = (struct stfu_holding_data*) ud;
  data->fired = true;
}

int signpost_tock_firmware_update_write_buffer(uint8_t* buffer, uint32_t offset, uint32_t length) {
  int ret;

  ret = subscribe(DRIVER_NUM_STFU_HOLDING, 1, stfu_holding_cb, (void*) &result);
  if (ret < 0) return ret;

  ret = allow(DRIVER_NUM_STFU_HOLDING, 1, (void*) buffer, length);
  if (ret < 0) return ret;

  uint32_t arg0 = (length << 8) | 3;
  ret = command(DRIVER_NUM_STFU_HOLDING, (int) arg0, (int) offset);
  if (ret < 0) return ret;

  // Wait for the callback.
  yield_for(&result.fired);

  return 0;
}
