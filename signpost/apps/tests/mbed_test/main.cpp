#include "mbed.h"
#include <stdio.h>
#include "port_signpost.h"
#include "signpost_api.h"
#include "signbus_io_interface.h"

int main(void) {
    printf("Testing mbed Initialization\n");

    int rc;
    do {
        rc = signpost_init("mbed_test");
        if (rc < 0) {
            printf(" - Error initializing module (code: %d). Sleeping 5s.\n", rc);
            port_signpost_delay_ms(5000);
        }
    } while (rc < 0);

    while(1) {
        port_signpost_debug_led_on();
        wait(0.5);
        port_signpost_debug_led_off();
        wait(0.5);
    }
}
