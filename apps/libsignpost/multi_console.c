#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <tock.h>
#include "multi_console.h"

typedef struct {
    int len;
    bool fired;
} console_callback_struct;


static void console_callback (
        __attribute__ ((unused)) int len,
        __attribute__ ((unused)) int x,
        __attribute__ ((unused)) int y,
        void* userdata) {

   console_callback_struct* cp = (console_callback_struct*)userdata;
   cp->fired = true;
   cp->len = len;
}

int console_read(int console_num, uint8_t* buf, size_t max_len) {
    int ret = allow(console_num, 0, (void*)buf, max_len);
    if(ret < 0) return ret;

    console_callback_struct c;
    c.fired = false;
    ret = subscribe(console_num, 2, console_callback, &c);
    if(ret < 0) return ret;

    yield_for(&c.fired);

    return c.len;
}

int console_read_async(int console_num, uint8_t* buf, size_t max_len, subscribe_cb cb) {
    int ret = allow(console_num, 0, (void*)buf, max_len);
    if(ret < 0) return ret;
    ret = subscribe(console_num, 2, cb, NULL);
    if(ret < 0) return ret;
}

int console_write(int console_num, uint8_t* buf, size_t count) {

    uint8_t* cbuf = (uint8_t*)malloc(count * sizeof(uint8_t));
    memcpy(cbuf, buf, count);

    int ret = allow(console_num, 1, (void*)cbuf, count);
    if(ret < 0) return ret;

    console_callback_struct c;
    c.fired = false;

    ret = subscribe(console_num, 1, console_callback, &c);
    if(ret < 0) return ret;

    yield_for(&c.fired);

    return c.len;
}

int console_write_async(int console_num, uint8_t* buf, size_t count, subscribe_cb cb) {
    int ret = allow(console_num, 1, (void*)buf, count);
    if(ret < 0) return ret;
    ret = subscribe(console_num, 1, cb, NULL);
    if(ret < 0) return ret;
}
