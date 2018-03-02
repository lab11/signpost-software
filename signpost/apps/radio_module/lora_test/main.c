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
        ret = xdot_send((uint8_t*)"Hi From Lab11",13);

        if(ret < 0) {
            printf("Xdot send failed\n");
        } else {

            printf("Xdot send succeeded\n");

            printf("Receiving data\n\n");
            uint8_t rx[100];
            ret = xdot_receive(rx, 100);
            if(ret < 0) {
                printf("Error receiving data\n");
            } else if(ret == 0) {
                printf("No data to receive\n");
            } else {
                printf("Data: 0x");
                for (uint8_t i = 0; i < ret; i++) {
                    printf("%02X",rx[i]);
                }
                printf("\n");
            }
        }

        xdot_sleep();
        delay_ms(5000);
    }
}
