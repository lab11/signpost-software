#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <timer.h>
#include <tock.h>

#include "signpost_api.h"

#define DATA_SIZE 600
static uint8_t data[DATA_SIZE] = {0};

int main (void) {
  int err;
  printf("[Test] API: Storage\n");

  do {
    err = signpost_init("storage_test");
    if (err < 0) {
      printf(" - Error initializing module (code %d). Sleeping 5s.\n", err);
      delay_ms(5000);
    }
  } while (err < 0);

  Storage_Record_t record = {.logname = "test"};

  while (true) {
    // create buffer
    // set buffer value to a byte from the record so that the value changes
    printf("Writing buffer!\n");

    err = signpost_storage_write(data, DATA_SIZE, &record);
    if (err < TOCK_SUCCESS) {
      printf("Error writing to storage: %d\n", err);
    } else {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
      printf("Wrote successfully!\n");
#pragma GCC diagnostic pop
    }
    printf("\n");

    // sleep for a second
    delay_ms(1000);
  }
}

