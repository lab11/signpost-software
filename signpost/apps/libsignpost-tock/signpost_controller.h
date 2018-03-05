#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "signpost_api.h"

int signpost_controller_init (void);
void signpost_controller_get_gps(signpost_timelocation_time_t* time, signpost_timelocation_location_t* location);
void signpost_controller_app_watchdog_tickle (void);
void signpost_controller_hardware_watchdog_tickle (void);

#ifdef __cplusplus
}
#endif
