#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "at_command.h"
#include "multi_console.h"


static int check_buffer(uint8_t* buf, int len) {
    //did it end in OK or ERROR?
    if(len >= 4) {
        if(!strncmp((char*)buf+len-4,"OK\r\n",4)) {
            return AT_SUCCESS;
        }
    }

    if(len >= 7) {
        if(!strncmp((char*)buf+len-7,"ERROR\r\n",7)) {
            return AT_ERROR;
        }
    }

    return AT_NO_RESPONSE;
}

static int check_custom_buffer(uint8_t* buf, int len, const char* rstring, int position) {

    int rlen = strlen(rstring);
    if(len >= rlen) {
        if(position > 0 && position < len-rlen) {
            if(!strncmp((char*)buf+position,rstring,rlen)) {
                return AT_SUCCESS;
            }
        } else {
            if(!strncmp((char*)buf+len-rlen,rstring,rlen)) {
                return AT_SUCCESS;
            }
        }
    }

    return AT_NO_RESPONSE;
}

int at_send(int console_num, const char* cmd) {
    int ret = console_write(console_num, (uint8_t*) cmd, strlen(cmd));
    if(ret < 0) return AT_ERROR;
    else return ret;
}

int at_send_buf(int console_num, uint8_t* buf, size_t len) {
    int ret = console_write(console_num, buf, len);
    if(ret < 0) return AT_ERROR;
    else return ret;
}

int at_wait_for_response(int console_num, uint8_t max_tries, uint32_t timeout_ms) {
    static uint8_t buf[200];
    return at_get_response(console_num, max_tries, timeout_ms, buf, 200);
}

int at_wait_for_custom_response(int console_num, uint8_t max_tries, uint32_t timeout_ms, const char* rstring, int position) {
    static uint8_t buf[200];
    return at_get_custom_response(console_num, max_tries, timeout_ms, buf, 200, rstring, position);
}

int at_get_response(int console_num, uint8_t max_tries, uint32_t timeout_ms, uint8_t* buf, size_t max_len) {

    int tlen = 0;
    for(uint8_t i = 0; i < max_tries; i++) {

        int len = console_read_with_timeout(console_num, buf+tlen, max_len-tlen, timeout_ms);
        if(len < 0) return AT_ERROR;

        tlen += len;
        int check = check_buffer(buf, tlen);

        if(check == AT_SUCCESS) {
            return tlen;
        } else if (check == AT_ERROR) {
            return AT_ERROR;
        }
    }

    return AT_NO_RESPONSE;
}

int at_get_custom_response(int console_num, uint8_t max_tries, uint32_t timeout_ms, uint8_t* buf, size_t max_len, const char* rstring, int position) {

    int tlen = 0;
    for(uint8_t i = 0; i < max_tries; i++) {

        int len = console_read_with_timeout(console_num, buf+tlen, max_len-tlen, timeout_ms);
        if(len < 0) return AT_ERROR;

        tlen += len;
        int check = check_custom_buffer(buf, tlen, rstring, position);

        if(check == AT_SUCCESS) {
            return tlen;
        } else if (check == AT_ERROR) {
            return AT_ERROR;
        }
    }

    return AT_NO_RESPONSE;
}
