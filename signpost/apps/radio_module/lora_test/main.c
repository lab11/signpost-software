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
#include "xdot.h"
#include "led.h"

#ifndef APP_KEY
#error Missing required define APP_KEY of format: 0x00, 0x00,... (x32)
#endif
static uint8_t appKey[16] = { APP_KEY };
static uint8_t appEUI[8] = {0};

int main (void) {
    printf("Starting Lora Test\n");


    printf("Initializing...\n");
    xdot_wake();
    int ret = xdot_init();
    if(ret < 0) {
        printf("Error Initializing\n");
    }

    ret = xdot_set_ack(1);
    ret |= xdot_set_txdr(3);
    ret |= xdot_set_txpwr(20);
    ret |= xdot_set_adr(0);
    if(ret < 0) printf("Settings error\n");

    do {
        printf("Joining network...\n");
        ret = xdot_join_network(appEUI, appKey);
        if(ret < 0) {
            printf("Error joining the network\n");
            delay_ms(5000);
            xdot_wake();
        }
    } while(ret < 0);


    while(1) {
        xdot_wake();

        printf("Sending data\n\n");
        xdot_send((uint8_t*)"Hi From Lab11",13);
        xdot_sleep();
        delay_ms(1000);
    }
}
