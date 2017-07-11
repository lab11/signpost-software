#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <tock.h>

#include "fm25cl.h"

uint8_t read_buf[256];
uint8_t write_buf[256];

static void print_buf (void) {
  printf("\tData: 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n\n", read_buf[0], read_buf[1], read_buf[2], read_buf[3], read_buf[4]);
}

int main (void) {
  printf("[FM25CL] Test\n");

  fm25cl_set_read_buffer(read_buf, 256);
  fm25cl_set_write_buffer(write_buf, 256);

  write_buf[0] = 0x02;
  write_buf[1] = 0x06;
  write_buf[2] = 0x0a;
  write_buf[3] = 0x0e;
  write_buf[4] = 0x9f;

  // Write buf to FRAM and then read it back
  fm25cl_write_sync(0x24, 5);
  fm25cl_read_sync(0x24, 5);

  for (int i=0; i<5; i++) {
    if (read_buf[i] != write_buf[i]) {
      printf("ERROR: mismatched bytes\n");
      print_buf();
      return;
    }
  }

  printf("Data written and read successfully\n");
}
