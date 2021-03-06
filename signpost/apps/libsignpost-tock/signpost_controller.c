#include "app_watchdog.h"
#include "signpost_controller.h"
#include "signpost_energy_policy.h"
#include "signpost_api.h"
#include "port_signpost.h"
#include "controller.h"
#include "timer.h"
#include "gps.h"
#include "fm25cl.h"
#include <stdio.h>
#include <string.h>

//watchdog enum
#define NUM_WATCH_STATES 3
static bool watchdog_states[NUM_WATCH_STATES] = {false};
typedef enum {
  WATCH_APP_TICKLE = 0,
  WATCH_LIB_GPS,
  //WATCH_LIB_ENERGY,
  WATCH_LIB_INIT,
} watchdog_tickler_t;

//module state struct
typedef enum {
    ModuleEnabled = 0,
    ModuleDisabledIsolation = 1,
    ModuleDisabledEnergy = 2,
    ModuleDisabledDutyCycle = 3,
    ModuleDisabledOff = 4,
} signpost_controller_module_isolation_e;

typedef struct module_state {
    uint8_t address;
    uint8_t initialized;
    signpost_controller_module_isolation_e isolation_state;
    uint8_t watchdog_subscribed;
    uint8_t watchdog_tickled;
    uint8_t module_init_failures;
} signpost_controller_module_state_t;

signpost_controller_module_state_t module_state[8] = {0};

//isolation bookeeping
int mod_isolated_out = -1;
int mod_isolated_in = -1;
int last_mod_isolated_out = -1;
size_t isolated_count = 0;
size_t isolation_timeout_seconds = 10;
uint8_t last_mod_out_state[NUM_MOD_IO] = {0};

//time and location local
static signpost_timelocation_time_t current_time;
static signpost_timelocation_location_t current_location;

//FRAM and persistent energy storage data
typedef struct {
  uint32_t magic;
  signpost_energy_remaining_t remaining;
  signpost_energy_used_t used;
  signpost_energy_time_since_reset_t time;
} controller_fram_t;
uint8_t fm25cl_read_buf[256];
uint8_t fm25cl_write_buf[256];
controller_fram_t fram;

static bool hard_reset = false;

extern module_state_t module_info;

static void app_watchdog_combine (watchdog_tickler_t which) {
  watchdog_states[(size_t)which] = true;

  bool all = true;
  for (int i = 0; i < NUM_WATCH_STATES; i++) {
    all = all && watchdog_states[i];
  }
  if (all) {
    app_watchdog_tickle_kernel();
    memset(watchdog_states, 0, NUM_WATCH_STATES*sizeof(watchdog_states[0]));
  }
}

static void enable_all_enabled_i2c(void) {
    for(uint8_t i = 0; i < 8; i++) {
        if(module_state[i].isolation_state == ModuleEnabled) {
            controller_module_enable_i2c(i);
        }
    }
}

static void initialization_api_callback(uint8_t source_address,
    signbus_frame_type_t frame_type, signbus_api_type_t api_type,
    uint8_t message_type, __attribute__ ((unused)) size_t message_length,
    uint8_t* message) {
    if (api_type != InitializationApiType) {
      signpost_api_error_reply_repeating(source_address, api_type, message_type, PORT_EINVAL, true, true, 1);
      return;
    }
    uint8_t req_mod_num = MODOUT_pin_to_mod_name(mod_isolated_out);
    int rc;
    switch (frame_type) {
        case NotificationFrame:
            // XXX unexpected, drop
            break;
        case CommandFrame:
            switch (message_type) {
                case InitializationDeclare: {

                    // only if we have a module isolated
                    if (mod_isolated_out < 0) {
                        return;
                    }

                    uint8_t new_address;
                    if(source_address == 0x00) {
                        new_address = 0x10 + req_mod_num;
                    } else {
                        new_address = source_address;
                    }

                    char name[17] = {0};
                    strncpy(name,(char*)message,message_length);

                    rc = signpost_initialization_declare_respond(source_address, new_address, req_mod_num, name);

                    if (rc < 0) {
                      //printf(" - %d: Error responding to initialization declare request for module %d at address 0x%02x. Dropping.\n",
                       //   __LINE__, req_mod_num, source_address);
                    }
                    break;
                }
                case InitializationGetState: {
                    signpost_initialization_get_module_state_reply(source_address);
                    break;
                }
                default:
                   break;
            }
        case ResponseFrame:
            // XXX unexpected, drop
            break;
        case ErrorFrame:
            // XXX unexpected, drop
            break;
        default:
            break;
    }
}

static bool checking_init = false;
static void check_module_init_cb( __attribute__ ((unused)) int now,
                            __attribute__ ((unused)) int expiration,
                            __attribute__ ((unused)) int unused,
                            __attribute__ ((unused)) void* ud) {


    if(checking_init) return;

    checking_init = true;

    //tickle watchdog
    app_watchdog_combine(WATCH_LIB_INIT);

    if (mod_isolated_out < 0) {
        for (size_t i = 0; i < NUM_MOD_IO; i++) {
            if (gpio_read(MOD_OUTS[i]) == 0 && last_mod_isolated_out != MOD_OUTS[i] && last_mod_out_state[i] == 1 &&
                                                module_state[MODOUT_pin_to_mod_name(MOD_OUTS[i])].isolation_state == ModuleEnabled) {

                printf("ISOLATION: Module %d granted isolation\n", MODOUT_pin_to_mod_name(MOD_OUTS[i]));
                // module requesting isolation
                mod_isolated_out = MOD_OUTS[i];
                mod_isolated_in = MOD_INS[i];
                last_mod_isolated_out = MOD_OUTS[i];
                last_mod_out_state[i] = 0;
                isolated_count = 0;

                // create private channel for this module
                //XXX warn modules of i2c disable
                controller_all_modules_disable_i2c();
                controller_module_enable_i2c(MODOUT_pin_to_mod_name(mod_isolated_out));
                // signal to module that it has a private channel
                // XXX this should be a controller function operating on the
                // module number, not index
                gpio_clear(mod_isolated_in);
                delay_ms(1000);
                break;
            }
            // didn't isolate anyone, reset last_mod_isolated_out
            last_mod_isolated_out = -1;
        }
    } else {
        if (gpio_read(mod_isolated_out) == 1) {
            printf("ISOLATION: Module %d done with isolation\n", MODOUT_pin_to_mod_name(mod_isolated_out));
            gpio_set(mod_isolated_in);
            mod_isolated_out = -1;
            mod_isolated_in  = -1;
            enable_all_enabled_i2c();
            module_state[MODOUT_pin_to_mod_name(mod_isolated_out)].module_init_failures = 0;
        }
        // this module took too long to talk to controller
        // XXX need more to police bad modules (repeat offenders)
        else if (isolated_count > isolation_timeout_seconds) {
            printf("ISOLATION: Module %d took too long\n", MODOUT_pin_to_mod_name(mod_isolated_out));
            gpio_set(mod_isolated_in);
            module_state[MODOUT_pin_to_mod_name(mod_isolated_out)].module_init_failures++;
            if(module_state[MODOUT_pin_to_mod_name(mod_isolated_out)].module_init_failures > 4) {
                //power cycle the module
                printf("Module %d has too many initialization failures - resetting\n",MODOUT_pin_to_mod_name(mod_isolated_out));
                controller_module_disable_power(MODOUT_pin_to_mod_name(mod_isolated_out));
                delay_ms(1000);
                controller_module_enable_power(MODOUT_pin_to_mod_name(mod_isolated_out));
                module_state[MODOUT_pin_to_mod_name(mod_isolated_out)].module_init_failures = 0;
            }
            mod_isolated_out = -1;
            mod_isolated_in  = -1;
            enable_all_enabled_i2c();
        } else {
          isolated_count++;
        }
    }

    //this give a module another chance if they pull their isolation pin up;
    for(size_t i = 0; i < NUM_MOD_IO; i++) {
        if(last_mod_out_state[i] == 0 && gpio_read(MOD_OUTS[i]) == 1) {
            last_mod_out_state[i] = 1;
        }
    }

    checking_init = false;
}

typedef struct duty_cycle_struct {
    int mod_num;
    tock_timer_t timer;
} duty_cycle_struct_t;


static void duty_cycle_timer_cb( __attribute__ ((unused)) int now,
                            __attribute__ ((unused)) int expiration,
                            __attribute__ ((unused)) int unused,
                            void* ud) {
    duty_cycle_struct_t* dc = ud;
    printf("Got duty cycle timer callback\n");

    if(signpost_energy_policy_get_module_energy_remaining_uwh(dc->mod_num) > 0 &&
            module_state[dc->mod_num].isolation_state == ModuleDisabledDutyCycle) {
        //printf("Turning module %d back on\n",dc->mod_num);
        module_state[dc->mod_num].isolation_state = ModuleEnabled;
        controller_module_enable_power(dc->mod_num);
        controller_module_enable_i2c(dc->mod_num);
    } else if (module_state[dc->mod_num].isolation_state == ModuleDisabledOff) {
        //printf("Module %d is manually off - leaving off\n",dc->mod_num);
    } else{
        //printf("Module %d has used too much energy - leaving off\n",dc->mod_num);
        module_state[dc->mod_num].isolation_state = ModuleDisabledEnergy;
    }

    free(dc);
}

static void energy_api_callback(uint8_t source_address,
    signbus_frame_type_t frame_type, signbus_api_type_t api_type,
    uint8_t message_type, __attribute__((unused)) size_t message_length, uint8_t* message) {

  //printf("CALLBACK_ENERGY: received energy api callback of type %d\n",message_type);

  if (api_type != EnergyApiType) {
    signpost_api_error_reply_repeating(source_address, api_type, message_type, PORT_EINVAL, true, true, 1);
    return;
  }

  int rc;

  if (frame_type == NotificationFrame) {
      if(message_type == EnergyDutyCycleMessage) {
        //printf("CALLBACK_ENERGY: received duty cycle request\n");
        //this is a duty cycle message
        //
        //how long does the module want to be duty cycled?
        uint32_t time;
        memcpy(&time, message, 4);

        //what module slot wants to be duty cycled?
        uint8_t mod = signpost_api_addr_to_mod_num(source_address);

        //create a timer with the module number to be turned back
        //on as the user data
        duty_cycle_struct_t* dc = malloc(sizeof(duty_cycle_struct_t));
        dc->mod_num = mod;

        timer_in(time, duty_cycle_timer_cb, (void*)dc, &(dc->timer));
        module_state[mod].isolation_state = ModuleDisabledDutyCycle;

        //turn it off
        printf("CALLBACK_ENERGY: Turning off module %d for %lums\n", mod, time);
        controller_module_disable_power(mod);
        controller_module_disable_i2c(mod);
      }
  } else if (frame_type == CommandFrame) {
    if (message_type == EnergyQueryMessage) {

      signpost_energy_information_t info;

      int mod_num = signpost_api_addr_to_mod_num(source_address);

      info.energy_limit_uWh = (int)(signpost_energy_policy_get_module_energy_remaining_uwh(mod_num));
      info.energy_used_since_reset_uWh = (int)(signpost_energy_policy_get_module_energy_used_uwh(mod_num));
      info.time_since_reset_s = (int)(signpost_energy_policy_get_time_since_module_reset_ms(mod_num)/1000.0);
      info.energy_limit_warning_threshold = (info.energy_limit_uWh < 1000000);
      info.energy_limit_critical_threshold = (info.energy_limit_uWh < 200000);
      //printf("Got energy query:\n");
      printf("\tLimit: %lu uWh\n",info.energy_limit_uWh);
      printf("\tUsed: %lu uWh\n",info.energy_used_since_reset_uWh);
      printf("\tTime: %lu s\n",info.time_since_reset_s);

      rc = signpost_energy_query_reply(source_address, &info);
      if (rc < 0) {
        //printf(" - %d: Error sending energy query reply (code: %d). Replying with fail.\n", __LINE__, rc);
        signpost_api_error_reply_repeating(source_address, api_type, message_type, PORT_EI2C_WRITE, true, true, 1);
      }

    } else if (message_type == EnergyReportModuleConsumptionMessage) {
        //this is for the radio to report other module's energy usage
        printf("CALLBACK_ENERGY: Received energy report from 0x%.2x\n", source_address);

        //first we should get the message and unpack it
        signpost_energy_report_t report;
        report.num_reports = message[0];

        //allocate memory for the reports
        signpost_energy_report_module_t* reps = malloc(report.num_reports*sizeof(signpost_energy_report_module_t));
        if(!reps) {
            printf("Error no memory for reports!\n");
            signpost_energy_report_reply(source_address, 0);
        }
        memcpy(reps, message+1, report.num_reports*sizeof(signpost_energy_report_module_t));
        report.reports = reps;

        //now send the report to the energy
        printf("Sending energy report to energy policy handler\n");
        signpost_energy_policy_update_energy_from_report(signpost_api_addr_to_mod_num(source_address), &report);

        free(reps);

        //reply to the report
        signpost_energy_report_reply(source_address, 1);
    } else if (message_type == EnergyResetMessage) {
        printf("CALLBACK_ENERGY: Received energy reset message from 0x%.2x\n", source_address);

        signpost_energy_policy_reset_module_energy_used(signpost_api_addr_to_mod_num(source_address));

        //reply
        signpost_energy_reset_reply(source_address, 1);
    } else {
      signpost_api_error_reply_repeating(source_address, api_type, message_type, PORT_ENOSUPPORT, true, true, 1);
    }
  } else if (frame_type == ResponseFrame) {
    // XXX unexpected, drop
  } else if (frame_type == ErrorFrame) {
    // XXX unexpected, drop
  }
}

// Callback for when a different module requests time or location information.
static void timelocation_api_callback(uint8_t source_address,
    signbus_frame_type_t frame_type, signbus_api_type_t api_type,
    uint8_t message_type, __attribute__ ((unused)) size_t message_length, __attribute__ ((unused)) uint8_t* message) {
  int rc;

  if (api_type != TimeLocationApiType) {
    signpost_api_error_reply_repeating(source_address, api_type, message_type, PORT_EINVAL, true, true, 1);
    return;
  }

  if (frame_type == NotificationFrame) {
    // XXX unexpected, drop
  } else if (frame_type == CommandFrame) {
    if (message_type == TimeLocationGetTimeMessage) {
      // Copy in time data from the most recent time we got an update
      // from the GPS.
      signpost_timelocation_time_t time;
      memcpy(&time,&current_time,sizeof(signpost_timelocation_time_t));
      rc = signpost_timelocation_get_time_reply(source_address, &time);
      if (rc < 0) {
        printf(" - %d: Error sending TimeLocationGetTimeMessage reply (code: %d).\n", __LINE__, rc);
        signpost_api_error_reply_repeating(source_address, api_type, message_type, PORT_EI2C_WRITE, true, true, 1);
      }

    } else if (message_type == TimeLocationGetLocationMessage) {
      signpost_timelocation_location_t location;
      memcpy(&location,&current_location,sizeof(signpost_timelocation_location_t));
      rc = signpost_timelocation_get_location_reply(source_address, &location);
      if (rc < 0) {
        printf(" - %d: Error sending TimeLocationGetLocationMessage reply (code: %d).\n", __LINE__, rc);
        signpost_api_error_reply_repeating(source_address, api_type, message_type, PORT_EI2C_WRITE, true, true, 1);
      }
    }
  } else if (frame_type == ResponseFrame) {
    // XXX unexpected, drop
  } else if (frame_type == ErrorFrame) {
    // XXX unexpected, drop
  }
}

static void watchdog_api_callback(uint8_t source_address,
    __attribute__ ((unused)) signbus_frame_type_t frame_type, __attribute__ ((unused)) signbus_api_type_t api_type,
    uint8_t message_type, __attribute__ ((unused)) size_t message_length, __attribute__ ((unused)) uint8_t* message) {

    printf("Got watchdog API call\n");

    if(message_type == WatchdogStartMessage) {
        int rc = signpost_watchdog_reply(source_address);
        if(rc >= 0) {
            int mod_num = signpost_api_addr_to_mod_num(source_address);
            module_state[mod_num].watchdog_subscribed = 1;

            //give them one tickle
            module_state[mod_num].watchdog_tickled = 1;
        }
    } else if(message_type == WatchdogTickleMessage)  {
        int rc = signpost_watchdog_reply(source_address);
        if(rc >= 0) {
            int mod_num = signpost_api_addr_to_mod_num(source_address);

            //give them one tickle
            module_state[mod_num].watchdog_tickled = 1;
        }
    }
}


void signpost_controller_get_gps(signpost_timelocation_time_t* time,
                                signpost_timelocation_location_t* location) {
    memcpy(time, &current_time, sizeof(signpost_timelocation_time_t));
    memcpy(location, &current_location, sizeof(signpost_timelocation_location_t));
}

static void gps_callback (gps_data_t* gps_data) {
  //tickle watchdog
  app_watchdog_combine(WATCH_LIB_GPS);

  // Save most recent GPS reading for anyone that wants location and time.
  // This isn't a great method for proving the TimeLocation API, but it's
  // pretty easy to do. We eventually need to reduce the sleep current of
  // the controller, which will mean better handling of GPS.
  current_time.year = gps_data->year + 2000;
  current_time.month = gps_data->month;
  current_time.day = gps_data->day;
  current_time.hours = gps_data->hours;
  current_time.minutes = gps_data->minutes;
  current_time.seconds = gps_data->seconds;
  current_time.satellite_count = gps_data->satellite_count;
  current_location.latitude = gps_data->latitude;
  current_location.longitude = gps_data->longitude;
  current_location.satellite_count = gps_data->satellite_count;

  // start sampling again to catch the next second
  gps_sample(gps_callback);
}

static void check_watchdogs_cb( __attribute__ ((unused)) int now,
                            __attribute__ ((unused)) int expiration,
                            __attribute__ ((unused)) int unused,
                            __attribute__ ((unused)) void* ud) {
    for(uint8_t j = 0; j < 8; j++) {
        if(module_state[j].watchdog_subscribed != 0) {
            if(module_state[j].watchdog_tickled == 0) {
                printf("Watchdog service not tickled - reseting module %d\n",j);
                controller_module_disable_power(j);
                delay_ms(300);
                controller_module_enable_power(j);
            } else {
                module_state[j].watchdog_tickled = 0;
            }
        }
    }
}

static void update_energy_policy_cb( __attribute__ ((unused)) int now,
                            __attribute__ ((unused)) int expiration,
                            __attribute__ ((unused)) int unused,
                            __attribute__ ((unused)) void* ud) {

    printf("Updating energy policy\n");
    //tickle watchdog
    //this policy updates less frequently than the watchdog does TODO fix that
    //app_watchdog_combine(WATCH_LIB_ENERGY);

    //run the update function
    signpost_energy_policy_update_energy();

    //save the values to nonvolatile memory
    signpost_energy_policy_copy_internal_state(&fram.remaining,&fram.used,&fram.time);
    fm25cl_write_sync(0, sizeof(controller_fram_t));

    printf("Battery energy remainding: %d uWh\n",signpost_energy_policy_get_battery_energy_remaining_uwh());
    printf("Controller energy remainding: %d uWh\n",signpost_energy_policy_get_controller_energy_remaining_uwh());

    //cut off any modules that have used too much
    for(uint8_t i = 0; i < 8; i++) {

        if(i == 3 || i == 4) continue;

        int remaining = signpost_energy_policy_get_module_energy_remaining_uwh(i);
        printf("Module %d has %d uWh remaining\n",i,remaining);
        if(signpost_energy_policy_get_module_energy_remaining_uwh(i) <= 0) {
            printf("Module %d used to much energy - disabling\n", i);
            module_state[i].isolation_state = ModuleDisabledEnergy;
            controller_module_disable_power(i);
            controller_module_disable_i2c(i);
        } else if (module_state[i].isolation_state == ModuleEnabled || module_state[i].isolation_state == ModuleDisabledEnergy) {
            module_state[i].isolation_state = ModuleEnabled;
            controller_module_enable_power(i);
            controller_module_enable_i2c(i);
        }
    }
}

static void signpost_controller_initialize_energy (void) {
    // Read FRAM to see if anything is stored there
    const unsigned FRAM_MAGIC_VALUE = 0x49C8000B;
    fm25cl_read_sync(0, sizeof(controller_fram_t));

    printf("Initializing energy\n");
    if (fram.magic == FRAM_MAGIC_VALUE) {
      // Great. We have saved data.
      // Initialize the energy algorithm with those values
      printf("Found saved energy data\n");
      signpost_energy_policy_init(&fram.remaining, &fram.used, &fram.time);

      signpost_energy_policy_copy_internal_state(&fram.remaining, &fram.used, &fram.time);

      fm25cl_write_sync(0, sizeof(controller_fram_t));
    } else {
      // Initialize this
      printf("No saved energy data. Equally distributing\n");
      fram.magic = FRAM_MAGIC_VALUE;

      //let the energy algorithm figure out how to initialize all the energies
      signpost_energy_policy_init(NULL, NULL, NULL);

      signpost_energy_policy_copy_internal_state(&fram.remaining, &fram.used, &fram.time);

      fm25cl_write_sync(0, sizeof(controller_fram_t));
    }
}

void signpost_controller_app_watchdog_tickle (void) {
  app_watchdog_combine(WATCH_APP_TICKLE);
}

void signpost_controller_hardware_watchdog_tickle (void) {
    if(!hard_reset) {
        gpio_clear(PIN_WATCHDOG);
        delay_ms(50);
        gpio_set(PIN_WATCHDOG);
    }
}

enum DownlinkCommand {
    ON = 1,
    OFF,
    RESET
};

static void downlink_cb(char* topic, uint8_t* data, uint8_t data_len) {
    //what commands do we know how to handler
    //topic = [module_name] on/off/reset
    //topic = signpost reset
    uint8_t command;
    if(!strncmp("on",(char*)data,data_len)) {
        command = ON;
    } else if(!strncmp("off",(char*)data,data_len)) {
        command = OFF;
    } else if(!strncmp("reset",(char*)data,data_len)) {
        command = RESET;
    } else {
        //we don't know this command
        return;
    }


    if(!strncmp("signpost",topic,NAME_LEN)) {
        if(command == RESET) {
            //go into failure mode and stop tickling the watchdog
            hard_reset = true;
        } else {
            //can't do anything else for the whole signpost
            return;
        }
    } else {
        //search for a module info that we can do something about
        int mod_num = signpost_api_module_name_to_mod_num(topic);
        if(mod_num >= 0) {
            switch(command) {
            case ON:
                if(signpost_energy_policy_get_module_energy_remaining_uwh(mod_num) > 0) {
                    //printf("Turning on %d\n",mod_num);
                    module_state[mod_num].isolation_state = ModuleEnabled;
                    controller_module_enable_power(mod_num);
                    controller_module_enable_i2c(mod_num);
                } else {
                    module_state[mod_num].isolation_state = ModuleDisabledEnergy;
                }
            break;
            case OFF:
                module_state[mod_num].isolation_state = ModuleDisabledOff;

                //turn it off
                controller_module_disable_power(mod_num);
                controller_module_disable_i2c(mod_num);
            break;
            case RESET:
                //printf("Resetting %d\n",mod_num);
                if(signpost_energy_policy_get_module_energy_remaining_uwh(mod_num) > 0 &&
                        module_state[mod_num].isolation_state == ModuleEnabled) {
                    controller_module_disable_power(mod_num);
                    delay_ms(300);
                    controller_module_enable_power(mod_num);
                }
            break;
            default:
                return;
            break;
            }

        } else {
            return;
        }
    }
}

int signpost_controller_init (void) {
    // Setup backplane by enabling the modules
    controller_init_module_switches();
    controller_all_modules_disable_power();
    controller_all_modules_disable_i2c();

    // setup app watchdog
    app_watchdog_set_kernel_timeout(180000);
    app_watchdog_start();
    // setup hardware watchdog
    gpio_enable_output(PIN_WATCHDOG);
    gpio_set(PIN_WATCHDOG);

    //we want a delay to prevent from accidentally initializing energy
    //during programming
    delay_ms(3000);

    //configure FRAM
    printf("Configuring FRAM\n");
    fm25cl_set_read_buffer((uint8_t*) &fram, sizeof(controller_fram_t));
    fm25cl_set_write_buffer((uint8_t*) &fram, sizeof(controller_fram_t));

    printf("Done with FRAM\n");

    //initialize energy from FRAM
    signpost_controller_initialize_energy();

    //initialize gps
    gps_init();
    gps_sample(gps_callback);

    //setup the signpost api
    static api_handler_t init_handler   = {InitializationApiType, initialization_api_callback};
    static api_handler_t energy_handler = {EnergyApiType, energy_api_callback};
    static api_handler_t timelocation_handler = {TimeLocationApiType, timelocation_api_callback};
    static api_handler_t watchdog_handler = {WatchdogApiType, watchdog_api_callback};
    static api_handler_t* handlers[] = {&init_handler, &energy_handler, &timelocation_handler, &watchdog_handler, NULL};

    int rc;
    do {
      rc = signpost_initialization_controller_module_init(handlers);
      if (rc < 0) {
        //printf(" - Error initializing as controller module with signpost library (code: %d)\n", rc);
        //printf("   Sleeping 5s\n");
        delay_ms(5000);
      }
    } while (rc < 0);

    delay_ms(500);

    //Make sure everything is initialized to the proper state
    controller_gpio_enable_all_MODINs();
    controller_gpio_enable_all_MODOUTs(PullUp);
    controller_gpio_set_all();
    controller_all_modules_enable_i2c();
    controller_all_modules_disable_usb();
    memset(last_mod_out_state, 1, NUM_MOD_IO);

    //Now enable all the modules
    controller_all_modules_enable_power();

    //setup timer callbacks to service the various signpost components
    static tock_timer_t energy_update_timer;
    timer_every(600000, update_energy_policy_cb, NULL, &energy_update_timer);

    //enable the init check tiemr
    static tock_timer_t check_init_timer;
    timer_every(300, check_module_init_cb, NULL, &check_init_timer);

    static tock_timer_t check_watchdogs_timer;
    timer_every(60000, check_watchdogs_cb, NULL, &check_watchdogs_timer);

    //setup the networking callback for radio downlink
    rc = signpost_networking_subscribe(downlink_cb);
    if(rc < TOCK_SUCCESS) {
        //printf("Downlink callback registration failed\n");
    }

    return 0;
}
