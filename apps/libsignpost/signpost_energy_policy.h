#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "signpost_api.h"

typedef struct energy_remaining {
    int controller_energy_remaining;
    int module_energy_remaining[8];
} signpost_energy_remaining_t;

typedef struct energy_used {
    int controller_energy_used;
    int linux_energy_used;
    int module_energy_used[8];
} signpost_energy_used_t;

typedef struct time_since_reset {
    uint32_t controller_time_since_reset;
    uint32_t linux_time_since_reset;
    uint32_t module_time_since_reset[8];
} signpost_energy_time_since_reset_t;

typedef struct energy_reset_start {
    uint32_t controller_energy_reset_start_time;
    uint32_t linux_energy_reset_start_time;
    uint32_t module_energy_reset_start_time[8];
} signpost_energy_reset_start_time_t;


//if r == NULL then initialize from battery capacity
void signpost_energy_policy_init(signpost_energy_remaining_t* remaining, 
                                 signpost_energy_used_t* used, 
                                 signpost_energy_time_since_reset_t* time);

//these functions tell you how much each module has remaining in their logical capacitors
//this is returned to the module on an energy query
int signpost_energy_policy_get_controller_energy_remaining_uwh (void);
int signpost_energy_policy_get_module_energy_remaining_uwh (int module_num);
int signpost_energy_policy_get_battery_energy_remaining_uwh (void);

//these functions tell you the energy used since the last reset of the module energy
int signpost_energy_policy_get_controller_energy_used_uwh (void);
int signpost_energy_policy_get_linux_energy_used_uwh (void);
int signpost_energy_policy_get_module_energy_used_uwh (int module_num);

//these functions reset the energy used field
void signpost_energy_policy_reset_controller_energy_used (void);
void signpost_energy_policy_reset_linux_energy_used (void);
void signpost_energy_policy_reset_module_energy_used (int module_num);

//these function return the time since the last energy reset
int signpost_energy_policy_get_time_since_controller_reset_ms (void);
int signpost_energy_policy_get_time_since_linux_reset_ms (void);
int signpost_energy_policy_get_time_since_module_reset_ms (int module_num);

//this function should be called every 600s to update energy capacities
void signpost_energy_policy_update_energy (void);

//this function should be called to distribute the energy of one module
//to other modules - primarily for the radio right now
void signpost_energy_policy_update_energy_from_report(uint8_t source_module_slot, signpost_energy_report_t* report);

#ifdef __cplusplus
}
#endif
