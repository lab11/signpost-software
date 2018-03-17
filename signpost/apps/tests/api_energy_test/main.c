#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <timer.h>
#include <tock.h>

#include "signpost_api.h"

int main (void) {
  printf("\n\n[Test] API: Energy\n");

  int rc;

  do {
    rc = signpost_init("energy_test");
    if (rc < 0) {
      printf(" - Error initializing module (code %d). Sleeping 5s.\n", rc);
      delay_ms(5000);
    }
  } while (rc < 0);

  signpost_energy_information_t info;

  while (true) {
    printf("\nQuery Energy\n");
    printf(  "============\n\n");
    rc = signpost_energy_query(&info);
    if (rc < TOCK_SUCCESS) {
      printf("Error querying energy: %d\n\n", rc);
    } else {
      printf("Energy Query Result:\n");
      printf("    energy used: %-4lu uWh\n", info.energy_used_since_reset_uWh);
      printf("   energy limit: %-4lu uWh\n", info.energy_limit_uWh);
      printf("          time : %-4lu s\n",  info.time_since_reset_s);
      printf("  mJ limit warn: %-4u %%\n",  info.energy_limit_warning_threshold);
      printf("  mJ limit crit: %-4u %%\n",  info.energy_limit_critical_threshold);
      printf("\n");
    }

    printf("Sleeping for 5s\n");
    delay_ms(5000);
  }
}
