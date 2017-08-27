#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define DRIVER_NUM_FM25CL 103

int fm25cl_set_read_buffer(uint8_t* buffer, uint32_t len);
int fm25cl_set_write_buffer(uint8_t* buffer, uint32_t len);

int fm25cl_read_sync(uint16_t address, uint16_t len);
int fm25cl_write_sync(uint16_t address, uint16_t len);

#ifdef __cplusplus
}
#endif
