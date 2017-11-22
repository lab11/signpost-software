#pragma once

#include <stdint.h>

#define DRIVER_NUM_STFU 0x11002
#define DRIVER_NUM_STFU_HOLDING 0x51000

int signpost_tock_firmware_update_go(uint32_t source,
                                     uint32_t destination,
                                     uint32_t length,
                                     uint32_t crc);

// Offset is in bytes, starting from 0. The app has a "virtual address space"
// from where it assigned in the board main.rs.
// Length is in bytes.
int signpost_tock_firmware_update_write_buffer(uint8_t* buffer, uint32_t offset, uint32_t length);
