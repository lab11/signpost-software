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
  printf("\n\n[Test] API: Time & Location\n");

  int rc = signpost_init("test","time");
  if (rc < TOCK_SUCCESS) {
    printf("Signpost initialization errored: %d\n", rc);
  }

  time_t t;
  static struct tm* ltime;
  signpost_timelocation_location_t location;

  while (true) {
    printf("Query Time\n");
    rc = signpost_timelocation_get_time(&t);
    ltime = gmtime(&t);
    if (rc < TOCK_SUCCESS) {
      printf("Error querying time: %d\n\n", rc);
    } else {
      printf("  Current time: %d/%d/%d %d:%02d:%02d with %d satellites\n", 1900+ltime->tm_year, ltime->tm_mon+1,
              ltime->tm_mday, ltime->tm_hour, ltime->tm_min, ltime->tm_sec, rc);
    }
    printf("Query Location\n");
    rc = signpost_timelocation_get_location(&location);
    if (rc < TOCK_SUCCESS) {
      printf("Error querying location: %d\n\n", rc);
    } else {
      float lat = ((float) location.latitude) / 1000000.0;
      float lon = ((float) location.longitude) / 1000000.0;
      printf("  Current location:\n");
      printf("    Latitude:  %f %lu\n", lat, location.latitude);
      printf("    Longitude: %f %lu\n", lon, location.longitude);
      printf("  With %d satellites\n",location.satellite_count);
    }

    printf("Sleeping for 5s\n\n");
    delay_ms(5000);
  }
}
