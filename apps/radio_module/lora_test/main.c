#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "console.h"
#include "led.h"
#include "timer.h"
#include "tock.h"
#include "xdot.h"

int main (void) {
  printf("Starting Lora Test\n");

  uint8_t AppEUI[8]  = {0xc0, 0x98, 0xe5, 0xc0, 0x00, 0x00, 0x00, 0x00};
  uint8_t AppKey[16] = {0};
  AppKey[15] = 0x01;

  printf("Initializing...\n");
  xdot_wake();
  int ret = xdot_init();
  if (ret < 0) {
    printf("Error Initializing\n");
  }

  printf("Joining network...\n");
  ret = xdot_join_network(AppEUI, AppKey);
  if (ret < 0) {
    printf("Error joining the network\n");
  }

  xdot_set_txdr(4);
  xdot_set_ack(1);
  xdot_set_txpwr(20);

  while (1) {
    xdot_wake();

    printf("Sending data\n\n");
    xdot_send((uint8_t*)"Hi From Lab11",13);
    xdot_sleep();
    delay_ms(1000);
  }
}
