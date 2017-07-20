#pragma once

#include <stdint.h>
#include <tock.h>

int console_read(int console_num, uint8_t* buf, size_t max_len);
int console_read_with_timeout(int console_num, uint8_t* buf, size_t max_len, uint32_t timeout_ms);
int console_read_async(int console_num, uint8_t* buf, size_t max_len, subscribe_cb cb);
int console_write(int console_num, uint8_t* buf, size_t count);
int console_write_async(int console_num, uint8_t* buf, size_t count, subscribe_cb cb);
