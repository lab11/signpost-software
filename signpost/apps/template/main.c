#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <timer.h>
#include <tock.h>

#include "signpost_api.h"

int main(void) {
    int rc;

    // Before an app can do anything useful, it needs to announce itself to the
    // signpost, negotiate keys, and learn what other services are available on
    // this signpost:
    do {
        rc = signpost_init("test","template");
        if (rc < 0) {
            printf(" - Error initializing module (code: %d). Sleeping 5s.\n", rc);
            delay_ms(5000);
        }
    } while (rc < 0);


    while(true) {
        /* YOUR CODE GOES HERE */

        delay_ms(1000);
    }
}
