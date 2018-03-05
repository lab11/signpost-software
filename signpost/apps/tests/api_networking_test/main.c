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

int main (void) {
  printf("[Networking Test] ** Main App **\n");

  int rc;

  /////////////////////////////
  // Signpost Module Operations
  //
  // Initializations for the rest of the signpost
  do {
    rc = signpost_initialization_module_init(0x29, NULL);
    if (rc < 0) {
      printf(" - Error initializing module (code: %d). Sleeping 5s.\n", rc);
      delay_ms(5000);
    }
  } while (rc < 0);

  printf("Initialized\n");

  const char* message = "Hello World!\n";
  while(1) {
      delay_ms(1000);
      printf("About to send\n");
      int result = signpost_networking_send("networking_test", (uint8_t*)message, strlen(message));
      if(result == 0) {
          printf("Send Succeeded!\n");
      } else {
          printf("Send Failed!\n");
      }
  }
}
