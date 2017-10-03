#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "timer.h"
#include "tock.h"

#include "signpost_api.h"
#include "signbus_io_interface.h"


int main(void) {
    printf("Update Test app\n");

    int rc;
    do {
        rc = signpost_initialization_module_init(0x32, SIGNPOST_INITIALIZATION_NO_APIS);
        if (rc < 0) {
            printf(" - Error initializing module (code: %d). Sleeping 5s.\n", rc);
            delay_ms(5000);
        }
    } while (rc < 0);

    delay_ms(20000);

    do {
        rc = signpost_update("ec2-52-43-58-157.us-west-2.compute.amazonaws.com/deployment/test_blink_bad","0.0.1",0,0x2000,0);
        if (rc < 0) {
            printf("Error code %d occurred during update\n", rc);
            delay_ms(20000);
        }
    } while (rc < 0);
}
