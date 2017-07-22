#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "signpost_api.h"

typedef enum {
  WATCH_GPS = 0,
  WATCH_ENERGY,
} watchdog_tickler_t;

int signpost_controller_init (void);
void signpost_controller_get_gps(signpost_timelocation_time_t* time, signpost_timelocation_location_t* location);
void app_watchdog_tickler (watchdog_tickler_t which);

#ifdef __cplusplus
}
#endif
