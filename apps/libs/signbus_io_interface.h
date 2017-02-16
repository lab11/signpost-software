#pragma once

#include <stdint.h>
#include <tock.h>
#include "signbus_app_layer.h"

//this is the first i2c messaging library!
//
//The MTU of the i2c bus is 256Bytes.
//We need packets
//Here's the proposed packet structure
//1:ver,2:total len, 3bit flag, remaind fragment offset, src

#ifdef __cplusplus
extern "C" {
#endif

// Defines for testing. These are arbitrary addresses.
#define SIGNBUS_TEST_SENDER_I2C_ADDRESS   0x32
#define SIGNBUS_TEST_RECEIVER_I2C_ADDRESS 0x18

/// initialize the library
/// MUST be called before any other methods.
void signbus_io_init(
    uint8_t address                   // Address to listen on / send from
    );

/// synchronous send
/// Returns number of bytes sent or < 0 on error.
int signbus_io_send(
    uint8_t dest,                     // Address to send to
    uint8_t* data,                    // Buffer to send from
    size_t len                        // Number of bytes to send
    );

/// synchronous receive
/// Returns number of bytes recieved or < 0 on error.
int signbus_io_recv(
    size_t   recv_buflen,             // Buffer length
    uint8_t* recv_buf,                // Buffer to receive into
    uint8_t* src_address              // Address received from
    );

/// async callback
/// len_or_rc is number of bytes received of < 0 on error.
typedef void (*signbus_io_callback_t)(int len_or_rc);

/// async receive
/// Returns < 0 on error.
int signbus_io_recv_async(
    signbus_io_callback_t callback,   // Called when recv operation completes
    size_t   recv_buflen,             // Buffer length
    uint8_t* recv_buf,                // Buffer to receive into
    uint8_t* src                      // Address received from
    );

//XXX Needs love: interface for erpc
//set the read buffer
void signbus_io_set_read_buffer(uint8_t* data, uint32_t len);

#ifdef __cplusplus
}
#endif
