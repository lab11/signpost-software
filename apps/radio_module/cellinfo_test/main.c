#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#include "tock.h"
#include "timer.h"
#include "console.h"
#include "led.h"
#include "sara_u260.h"


int main (void) {
    printf("Starting Cell Info Test\n");

    delay_ms(2000);

    sara_u260_init();

    delay_ms(5000);

    sara_u260_ops_info_t inf[10];

    while(1) {
        int num_returned = sara_u260_get_ops_information(inf,10);
        for(int i = 0; i < num_returned; i++) {
            printf("MCC: %hu, MNC: %hu, LAC: %hx, CI: %lx, BSIC: %hu, ARFCN: %hu,RXLEV: %hu\n",
                            inf[i].mcc,inf[i].mnc,inf[i].lac,inf[i].ci,inf[i].bsic,inf[i].arfcn,inf[i].rxlev);
        }
        delay_ms(5000);
    }
}
