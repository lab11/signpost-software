#include "signpost_energy_policy.h"
#include "signpost_energy_monitors.h"
#include "signpost_api.h"
#include "timer.h"
#include "alarm.h"
#include "internal/alarm.h"
#include <stdio.h>
#include <limits.h>
#include <string.h>

/////////////////////////////////////////////////////
//These are the RAM variables that we update
//We are going to store them in nonvolatile memory too
/////////////////////////////////////////////////////
signpost_energy_remaining_t energy_remaining;
signpost_energy_used_t energy_used;
signpost_energy_time_since_reset_t time_since_reset;
signpost_energy_reset_start_time_t energy_reset_start_time;

static int battery_last_energy_remaining = 0;
static int battery_energy_remaining;

#define BATTERY_CAPACITY 9000000*11.1
#define MAX_CONTROLLER_ENERGY_REMAINING BATTERY_CAPACITY*0.4
#define MAX_MODULE_ENERGY_REMAINING BATTERY_CAPACITY*0.1

void signpost_energy_policy_init (signpost_energy_remaining_t* remaining,
                                    signpost_energy_used_t* used,
                                    signpost_energy_time_since_reset_t* time) {


    if(remaining == NULL) {
        //initialize all of the energy remainings
        //...We really should do this in a nonvolatile way
        int battery_remaining = signpost_energy_get_battery_energy_uwh();
        energy_remaining.controller_energy_remaining = battery_remaining*0.4;
        for(uint8_t i = 0; i < 8; i++) {
            if(i == 4 || i == 3) {

            } else {
                energy_remaining.module_energy_remaining[i] = battery_remaining*0.1;
            }
        }
    } else {
        memcpy(&energy_remaining,remaining,sizeof(signpost_energy_remaining_t));
    }

    if(used == NULL || time == NULL) {
        //if we don't have any data from the last reset we will just reset
        //everything to zero and restart the timers
        signpost_energy_policy_reset_controller_energy_used();
        signpost_energy_policy_reset_linux_energy_used();
        for(uint8_t i = 0; i < 8; i++) {
            if(i == 4 || i == 3) {

            } else {
                signpost_energy_policy_reset_module_energy_used(i);
            }
        }
    } else {
        memcpy(&energy_used,used,sizeof(signpost_energy_used_t));
        memcpy(&time_since_reset,time,sizeof(signpost_energy_time_since_reset_t));
    }

    //configure the coulomb counters
    signpost_energy_init_ltc2943();

    //reset all of the coulomb counters for the algorithm to work
    signpost_energy_reset_all_energy();

    //read the battery now so that the first interation works
    int bat = signpost_energy_get_battery_energy_uwh();
    battery_last_energy_remaining = bat;
}

////////////////////////////////////////////////////////////////
// These functions tell you how much energy the module has remaining in it's "capacitor"
// This is updated at every call to the update function
// /////////////////////////////////////////////////////////////

int signpost_energy_policy_get_controller_energy_remaining_uwh (void) {
    return (energy_remaining.controller_energy_remaining - signpost_energy_get_controller_energy_uwh());
}

int signpost_energy_policy_get_module_energy_remaining_uwh (int module_num) {
    return (energy_remaining.module_energy_remaining[module_num] - signpost_energy_get_module_energy_uwh(module_num));
}

int signpost_energy_policy_get_battery_energy_remaining_uwh (void) {
    return battery_energy_remaining;
}

////////////////////////////////////////////////////////////////
// These functions return the energy used since the last reset
////////////////////////////////////////////////////////////////

int signpost_energy_policy_get_controller_energy_used_uwh (void) {
    return (energy_used.controller_energy_used + signpost_energy_get_controller_energy_uwh());
}

int signpost_energy_policy_get_linux_energy_used_uwh (void) {
    return (energy_used.linux_energy_used + signpost_energy_get_linux_energy_uwh());
}

int signpost_energy_policy_get_module_energy_used_uwh (int module_num) {
    return (energy_used.module_energy_used[module_num] + signpost_energy_get_module_energy_uwh(module_num));
}

////////////////////////////////////////////////////////////////
// These functions reset the energy used counters
// They also reset the energy used timers
///////////////////////////////////////////////////////////////
void signpost_energy_policy_reset_controller_energy_used (void) {
    int energy = signpost_energy_get_controller_energy_uwh();
    energy_used.controller_energy_used = -1*energy;
    time_since_reset.controller_time_since_reset = 0;
    energy_reset_start_time.controller_energy_reset_start_time = alarm_read();
}

void signpost_energy_policy_reset_linux_energy_used (void) {
    int energy = signpost_energy_get_linux_energy_uwh();
    //this is a bit odd but it works
    //essentially we dont' want to reset the real counters yet
    //and used is virtual_used + counter
    //if we reset to virtual_used = -counter then when we read used it will be 0
    energy_used.linux_energy_used = -1*energy;
    time_since_reset.linux_time_since_reset = 0;
    energy_reset_start_time.linux_energy_reset_start_time = alarm_read();
}

void signpost_energy_policy_reset_module_energy_used (int module_num) {
    int energy = signpost_energy_get_module_energy_uwh(module_num);
    energy_used.module_energy_used[module_num] = -1*energy;
    time_since_reset.module_time_since_reset[module_num] = 0;
    energy_reset_start_time.module_energy_reset_start_time[module_num] = alarm_read();
}

/////////////////////////////////////////////////////////////////////
// These functions return the time on the timers
/////////////////////////////////////////////////////////////////////
int signpost_energy_policy_get_time_since_controller_reset_ms (void) {
    //calculate ms since last update
    uint32_t alarm_freq = alarm_internal_frequency();
    uint32_t now = alarm_read();
    uint32_t ms;
    if(now < energy_reset_start_time.controller_energy_reset_start_time) {
        ms = (uint32_t)(((((UINT32_MAX - energy_reset_start_time.controller_energy_reset_start_time) + now)*1.0)/alarm_freq)*1000);
    } else {
        ms = (uint32_t)((((now - energy_reset_start_time.controller_energy_reset_start_time)*1.0)/alarm_freq)*1000);
    }

    return time_since_reset.controller_time_since_reset + ms;
}

int signpost_energy_policy_get_time_since_linux_reset_ms (void) {
    //calculate ms since last update
    uint32_t alarm_freq = alarm_internal_frequency();
    uint32_t now = alarm_read();
    uint32_t ms;
    if(now < energy_reset_start_time.linux_energy_reset_start_time) {
        ms = (uint32_t)(((((UINT32_MAX - energy_reset_start_time.linux_energy_reset_start_time) + now)*1.0)/alarm_freq)*1000);
    } else {
        ms = (uint32_t)((((now - energy_reset_start_time.linux_energy_reset_start_time)*1.0)/alarm_freq)*1000);
    }

    return time_since_reset.linux_time_since_reset + ms;
}

int signpost_energy_policy_get_time_since_module_reset_ms (int module_num) {
    //calculate ms since last update
    uint32_t alarm_freq = alarm_internal_frequency();
    uint32_t now = alarm_read();
    uint32_t ms;
    if(now < energy_reset_start_time.module_energy_reset_start_time[module_num]) {
        ms = (uint32_t)(((((UINT32_MAX - energy_reset_start_time.module_energy_reset_start_time[module_num]) + now)*1.0)/alarm_freq)*1000);
    } else {
        ms = (uint32_t)((((now - energy_reset_start_time.module_energy_reset_start_time[module_num])*1.0)/alarm_freq)*1000);
    }

    return time_since_reset.module_time_since_reset[module_num] + ms;
}

////////////////////////////////////////////////////////////////////
// local functions for updating timers without reseting counters
///////////////////////////////////////////////////////////////////

static void signpost_energy_policy_reset_and_update_energy_timers (void) {
    time_since_reset.controller_time_since_reset = signpost_energy_policy_get_time_since_controller_reset_ms();
    energy_reset_start_time.controller_energy_reset_start_time = alarm_read();

    time_since_reset.linux_time_since_reset = signpost_energy_policy_get_time_since_linux_reset_ms();
    energy_reset_start_time.linux_energy_reset_start_time = alarm_read();

    for(uint8_t i = 0; i < 8; i++) {
    time_since_reset.module_time_since_reset[i] = signpost_energy_policy_get_time_since_module_reset_ms(i);
        energy_reset_start_time.module_energy_reset_start_time[i] = alarm_read();
    }
}


///////////////////////////////////////////////////////////////////
//The big update function
///////////////////////////////////////////////////////////////
void signpost_energy_policy_update_energy (void) {

    //At every update the procedure:
    // 1) read all of the coulomb counters
    // 2) update the energy_used fields by adding the value
    // 3) subtract the energy_remaining fields by subtracting the value
    // 4) update the time_since_reset fields
    // 5) calculate the energy harvesting over this time period
    // 6) consolidate harvested and total energy used
    // 7) distribute excess fairly to the modules

    //////////////////////////////////////////////////////////////////////
    // Read the coulomb counters
    /////////////////////////////////////////////////////////////////////

    //now let's read all the coulomb counters
    int total_energy = 0;
    int linux_energy = signpost_energy_get_linux_energy_uwh();
    printf("Linux used %d uWh\n",linux_energy);
    total_energy += linux_energy;

    int controller_energy = signpost_energy_get_controller_energy_uwh();
    printf("Controller used %d uWh\n",controller_energy);
    total_energy += controller_energy;

    int module_energy[8];

    for(uint8_t i = 0; i < 8; i++) {
        if(i == 3 || i == 4) {

        } else {
            module_energy[i] = signpost_energy_get_module_energy_uwh(i);
            printf("Module %d used %d uWh\n",i,module_energy[i]);
            total_energy += module_energy[i];
        }
    }
    battery_energy_remaining = signpost_energy_get_battery_energy_uwh();
    printf("Battery has %d uWh energy\n",battery_energy_remaining);
    //reset all of the coulomb counters so we can use them next time
    signpost_energy_reset_all_energy();

    ///////////////////////////////////////////////////////////////////
    // Update energy used fields by adding these energies
    ///////////////////////////////////////////////////////////////////
    energy_used.controller_energy_used += controller_energy;
    printf("Controller has used %d uWh energy since reset\n",energy_used.controller_energy_used);
    energy_used.linux_energy_used += linux_energy;
    printf("Linux has used %d uWh energy since reset\n",energy_used.linux_energy_used);
    for(uint8_t i = 0; i < 8; i++) {
        if(i == 3 || i == 4) {

        } else {
            energy_used.module_energy_used[i] += module_energy[i];
            printf("Module %d has used %d uWh energy since reset\n",i,energy_used.module_energy_used[i]);
        }
    }

    ///////////////////////////////////////////////////////////////////
    // Update energy remaining fields by subtracting these energies
    ///////////////////////////////////////////////////////////////////
    energy_remaining.controller_energy_remaining -= controller_energy;
    printf("Controller has %d uWh energy remaining\n",energy_remaining.controller_energy_remaining);
    energy_remaining.controller_energy_remaining -= linux_energy;
    printf("Controller has %d uWh energy remaining\n",energy_remaining.controller_energy_remaining);
    for(uint8_t i = 0; i < 8; i++) {
        if(i == 3 || i == 4) {

        } else {
            energy_remaining.module_energy_remaining[i] -= module_energy[i];
            printf("Module %d has %d uWh energy remaining\n",i,energy_remaining.module_energy_remaining[i]);
        }
    }

    ////////////////////////////////////////////////////////////////////
    // Update the timers
    ////////////////////////////////////////////////////////////////////
    signpost_energy_policy_reset_and_update_energy_timers();

    ////////////////////////////////////////////////////////////////////
    // Calculate the amount of energy we have used and harvested
    ////////////////////////////////////////////////////////////////////
    int battery_used = battery_last_energy_remaining-battery_energy_remaining;
    printf("Battery used %d uWh energy\n",battery_used);
    battery_last_energy_remaining = battery_energy_remaining;

    //theoretically battery_used = total_energy - solar_energy
    //Let's try to consolidate these numbers
    //we can't just distribute left over energy because the under-penalizes
    //high consumers

    //now we need to figure out how much energy (if any) we got
    //This needs to be distributed among the modules
    // technically battery_energy_remaining = battery_last_energy_remaining - total_energy_used + solar_energy
    // This isn't going to be true due to efficiency losses and such
    // But what we can do:
    if(battery_used < total_energy) {
        //we have surplus!! let's distribute it
        int surplus = total_energy - battery_used;
        int controller_surplus = (int)(surplus * 0.4);
        int module_surplus = (int)(surplus * 0.1);
        printf("Surplus energy of %d uwh\n",surplus);

        energy_remaining.controller_energy_remaining += controller_surplus;
        printf("Controller surplus of %d uwh\n",controller_surplus);
        if(energy_remaining.controller_energy_remaining > MAX_CONTROLLER_ENERGY_REMAINING) {
            module_surplus += (int)((energy_remaining.controller_energy_remaining - MAX_CONTROLLER_ENERGY_REMAINING)/6.0);
            energy_remaining.controller_energy_remaining = MAX_CONTROLLER_ENERGY_REMAINING;
            printf("Controller energy overflow, module surplus now %d\n",module_surplus);
        }

        //this algorithm distributes energy while redistributing full modules
        uint8_t spill_elgible[8] = {0};
        for(uint8_t i = 0; i < 8; i++) spill_elgible[i] = 1;

        while(module_surplus > 0) {
            int spill_over = 0;
            uint8_t spill_elgible_count = 0;

            //try to distribute the energy
            for(uint8_t i = 0; i < 8; i++) {
                if(i == 3 || i == 4 || spill_elgible[i] == 0) {
                    printf("Not adding energy to this module, spill elgible = %d\n",spill_elgible[i]);
                }

                if(energy_remaining.module_energy_remaining[i] + module_surplus > MAX_MODULE_ENERGY_REMAINING) {
                    spill_over += (energy_remaining.module_energy_remaining[i] + module_surplus) - MAX_MODULE_ENERGY_REMAINING;
                    energy_remaining.module_energy_remaining[i] = MAX_MODULE_ENERGY_REMAINING;
                    spill_elgible[i] = 0;
                    printf("Module %d energy overflow. spill over now %d\n",i,spill_over);
                } else {
                    energy_remaining.module_energy_remaining[i] += module_surplus;
                    printf("Module %d got %d uwh of energy from surplus\n",i,module_surplus);
                    spill_elgible_count++;
                    spill_elgible[i] = 1;
                }
            }

            //if everything is full give to controller, else distribute again
            if(spill_elgible_count == 0) {
                module_surplus = 0;
                energy_remaining.controller_energy_remaining += spill_over;
            } else {
                printf("splitting %d spill between %d modules\n",spill_over,spill_elgible_count);
                module_surplus = spill_over/spill_elgible_count;
            }
        }

    } else {
        //efficiency losses - we should probably also distribute those losses (or charge them to the controller?)
        energy_remaining.controller_energy_remaining -= battery_used - total_energy;
        printf("No energy surplus\n");
    }
}

void signpost_energy_policy_update_energy_from_report(uint8_t source_module_slot, signpost_energy_report_t* report) {
    uint8_t num_reports = report->num_reports;
    printf("Received %d reports from source module %d\n",num_reports, source_module_slot);
    for(uint8_t j = 0; j < num_reports; j++) {
        uint8_t mod_num = signpost_api_appid_to_mod_num(report->reports[j].application_id);
        printf("Usage Report for mod %02X in slot %d:\n",report->reports[j].application_id,mod_num);
        //take the energy since last report, add/subtract it from all the totals
        if(mod_num == 3) {
            printf("\tUsed %lu uWh\n",report->reports[j].energy_used_uWh);
            printf("\t\t%d uWh was remaining\n",energy_remaining.controller_energy_remaining);
            energy_remaining.controller_energy_remaining -= (int)(report->reports[j].energy_used_uWh);
            energy_used.controller_energy_used += (int)(report->reports[j].energy_used_uWh);
            printf("\t\t%d uWh now remaining\n", energy_remaining.controller_energy_remaining);

            printf("\tSource module %d had %d uWh remaining\n",source_module_slot,energy_remaining.module_energy_remaining[source_module_slot]);
            energy_remaining.module_energy_remaining[source_module_slot] += (int)(report->reports[j].energy_used_uWh);
            energy_used.module_energy_used[source_module_slot] -= (int)(report->reports[j].energy_used_uWh);
            printf("\tSource module %d now has %d uWh remaining\n",source_module_slot,energy_remaining.module_energy_remaining[source_module_slot]);
        } else {
            printf("\tUsed %lu uWh\n",report->reports[j].energy_used_uWh);
            printf("\t%d uWh was remaining\n",energy_remaining.module_energy_remaining[mod_num]);
            energy_remaining.module_energy_remaining[mod_num] -= (int)(report->reports[j].energy_used_uWh);
            energy_used.module_energy_used[mod_num] += (int)(report->reports[j].energy_used_uWh);
            printf("\t%d uWh now emaining\n", energy_remaining.module_energy_remaining[mod_num]);

            printf("\tSource module %d had %d uWh remaining\n",source_module_slot,energy_remaining.module_energy_remaining[source_module_slot]);
            energy_remaining.module_energy_remaining[source_module_slot] += (int)(report->reports[j].energy_used_uWh);
            energy_used.module_energy_used[source_module_slot] -= (int)(report->reports[j].energy_used_uWh);
            printf("\tSource module %d now has %d uWh remaining\n",source_module_slot,energy_remaining.module_energy_remaining[source_module_slot]);
        }
    }
}


void signpost_energy_policy_copy_internal_state(signpost_energy_remaining_t* remaining,
                                         signpost_energy_used_t* used,
                                         signpost_energy_time_since_reset_t* time) {
    memcpy(remaining,&energy_remaining,sizeof(signpost_energy_remaining_t));
    memcpy(used,&energy_used,sizeof(signpost_energy_used_t));
    memcpy(time,&time_since_reset,sizeof(signpost_energy_time_since_reset_t));
}

