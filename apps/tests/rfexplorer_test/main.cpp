#include "mbed.h"
#include <stdio.h>
#include "port_signpost.h"

int main(void) {
    printf("Testing mbed\n");

    while(1) {
        port_signpost_debug_led_on();
        wait(0.5);
        port_signpost_debug_led_off();
        wait(0.5);
    }
}
