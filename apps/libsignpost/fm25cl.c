
#include <tock.h>
#include <internal/nonvolatile_storage.h>

#include "fm25cl.h"

typedef struct {
  bool fired;
} fm25cl_data_t;

// Internal callback for faking synchronous reads
static void fm25cl_cb(__attribute__ ((unused)) int callback_type,
                      __attribute__ ((unused)) int len,
                      __attribute__ ((unused)) int unused,
                      void* ud) {
  fm25cl_data_t* data = (fm25cl_data_t*) ud;
  data->fired = true;
}

int fm25cl_set_read_buffer(uint8_t* buffer, uint32_t len) {
  return nonvolatile_storage_internal_read_buffer(buffer, len);
}

int fm25cl_set_write_buffer(uint8_t* buffer, uint32_t len) {
  return nonvolatile_storage_internal_write_buffer(buffer, len);
}

int fm25cl_read_sync(uint16_t address, uint16_t len) {
  int err;
  fm25cl_data_t result = {.fired = false};

  err = nonvolatile_storage_internal_read_done_subscribe(fm25cl_cb, (void*)&result);
  if (err < 0) return err;

  err = nonvolatile_storage_internal_read(address, len);
  if (err < 0) return err;

  // Wait for the callback.
  yield_for(&result.fired);

  return 0;
}

int fm25cl_write_sync(uint16_t address, uint16_t len) {
  int err;
  fm25cl_data_t result = {.fired = false};

  err = nonvolatile_storage_internal_write_done_subscribe(fm25cl_cb, (void*)&result);
  if (err < 0) return err;

  err = nonvolatile_storage_internal_write(address, len);
  if (err < 0) return err;

  // Wait for the callback.
  yield_for(&result.fired);

  return 0;
}
