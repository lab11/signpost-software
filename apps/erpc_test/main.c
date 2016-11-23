#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

#include <tock.h>
#include <firestorm.h>
#include <console.h>
#include "test_arithmetic.h"
#include "erpc_client_setup.h"
#include "erpc_transport_setup.h"


int main () {
    erpc_transport_t transport;
    transport = erpc_transport_serial_init("out",115200);
    erpc_client_init(transport);

  	while (1) {
        delay_ms(5000);
        putstr("About to test the rpc\n");
        delay_ms(5000);

        float result = 0;
        result =  add(5,6);

        if(result == 11) {
            //putstr("RPC Completed Correctly\n");
        } else {
            //putstr("RPC Failed\n");
        }
  	}
}
