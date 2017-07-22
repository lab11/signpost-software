#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "signpost_api.h"

int signpost_controller_init (void);
void signpost_controller_get_gps(signpost_timelocation_time_t* time, signpost_timelocation_location_t* location);
void app_watchdog_tickler (void);

#ifdef __cplusplus
}
#endif
