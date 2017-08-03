#include <stdint.h>
#include <stdbool.h>
#include <tock.h>

#define AT_SUCCESS 0
#define AT_ERROR -1
#define AT_NO_RESPONSE -2

__attribute__ ((warn_unused_result))
int at_send(int console_num, const char* cmd);

__attribute__ ((warn_unused_result))
int at_send_buf(int console_num, uint8_t* buf, size_t len);


__attribute__ ((warn_unused_result))
int at_wait_for_response(int console_num, uint8_t max_tries, uint32_t timeout_ms);

__attribute__ ((warn_unused_result))
int at_wait_for_custom_response(int console_num, uint8_t max_tries, uint32_t timeout_ms, const char* rstring, int position);

__attribute__ ((warn_unused_result))
int at_get_response(int console_num, uint8_t max_tries, uint32_t timeout_ms, uint8_t* buf, size_t max_len);

__attribute__ ((warn_unused_result))
int at_get_custom_response(int console_num, uint8_t max_tries, uint32_t timeout_ms, uint8_t* buf, size_t max_len, const char* rstring, int position);
