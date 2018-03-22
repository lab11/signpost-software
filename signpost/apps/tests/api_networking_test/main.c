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

static void downlink_cb(__attribute__ ((unused)) char* topic, uint8_t* data, uint8_t data_len) {
    printf("Received Downlink: %.*s\n",data_len, (char*)data);
}

int main (void) {
  printf("[Networking Test] ** Main App **\n");

  int rc;

  /////////////////////////////
  // Signpost Module Operations
  //
  // Initializations for the rest of the signpost
  do {
    rc = signpost_init("test","network");
    if (rc < 0) {
      printf(" - Error initializing module (code: %d). Sleeping 5s.\n", rc);
      delay_ms(5000);
    }
  } while (rc < 0);

  printf("Initialized\n");

  //subscribe to the networking subscribe function
  signpost_networking_subscribe(downlink_cb);

  const char* message = "Hello World!\n";
  while(1) {
      delay_ms(5000);
      printf("About to send\n");
      //publishing to network_test/echo generate a downlink message for testing to the same topic
      int result = signpost_networking_publish("echo", (uint8_t*)message, strlen(message));
      if(result == 0) {
          printf("Send Succeeded!\n");
      } else {
          printf("Send Failed!\n");
      }
  }
}
