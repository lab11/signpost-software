#include "app_watchdog.h"
#include "signpost_controller.h"
#include "signpost_energy_policy.h"
#include "signpost_api.h"
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
      signpost_api_error_reply_repeating(source_address, api_type, message_type, true, true, 1);
      return;
    }
    int module_number;
    int rc;
    switch (frame_type) {
        case NotificationFrame:
            // XXX unexpected, drop
            break;
        case CommandFrame:
            switch (message_type) {
                case InitializationRegister:
                    rc = signpost_initialization_register_respond(source_address);
                    if (rc < 0) {
                      printf(" - %d: Error responding to initialization register request for address 0x%02x. Dropping.\n",
                          __LINE__, source_address);
                    }

                case InitializationDeclare:
                    // only if we have a module isolated or from storage master
                    if (mod_isolated_out < 0 && source_address != ModuleAddressStorage) {
                        return;
                    }
                    if (source_address == ModuleAddressStorage)  {
                        module_number = 4;
                    }
                    else {
                        module_number = MODOUT_pin_to_mod_name(mod_isolated_out);
                    }

                    rc = signpost_initialization_declare_respond(source_address, module_number);

                    if (rc < 0) {
                      printf(" - %d: Error responding to initialization declare request for module %d at address 0x%02x. Dropping.\n",
                          __LINE__, module_number, source_address);
                    }
                    break;
                case InitializationKeyExchange:
                    // Prepare and reply ECDH key exchange
                    rc = signpost_initialization_key_exchange_respond(source_address,
                            message, message_length);
                    if (rc < 0) {
                      printf(" - %d: Error responding to key exchange at address 0x%02x. Dropping.\n",
                          __LINE__, source_address);
                    }
                    break;
                //exchange module
                //get mods
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

static void check_module_init_cb( __attribute__ ((unused)) int now,
                            __attribute__ ((unused)) int expiration,
                            __attribute__ ((unused)) int unused,
                            __attribute__ ((unused)) void* ud) {

    //tickle watchdog
    app_watchdog_combine(WATCH_LIB_INIT);

    if (mod_isolated_out < 0) {
        for (size_t i = 0; i < NUM_MOD_IO; i++) {
            if (gpio_read(MOD_OUTS[i]) == 0 && last_mod_isolated_out != MOD_OUTS[i]) {

                printf("ISOLATION: Module %d granted isolation\n", MODOUT_pin_to_mod_name(MOD_OUTS[i]));
                // module requesting isolation
                mod_isolated_out = MOD_OUTS[i];
                mod_isolated_in = MOD_INS[i];
                last_mod_isolated_out = MOD_OUTS[i];
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

    if(signpost_energy_policy_get_module_energy_remaining_uwh(dc->mod_num) > 0) {
        module_state[dc->mod_num].isolation_state = ModuleEnabled;
        controller_module_enable_power(dc->mod_num);
        controller_module_enable_i2c(dc->mod_num);
    } else {
        module_state[dc->mod_num].isolation_state = ModuleDisabledEnergy;
    }

    free(dc);
}

static void energy_api_callback(uint8_t source_address,
    signbus_frame_type_t frame_type, signbus_api_type_t api_type,
    uint8_t message_type, size_t message_length, uint8_t* message) {

  printf("CALLBACK_ENERGY: received energy api callback of type %d\n",message_type);

  if (api_type != EnergyApiType) {
    signpost_api_error_reply_repeating(source_address, api_type, message_type, true, true, 1);
    return;
  }

  int rc;

  if (frame_type == NotificationFrame) {
      if(message_type == EnergyDutyCycleMessage) {
        printf("CALLBACK_ENERGY: received duty cycle request\n");
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
        printf("CALLBACK_ENERGY: Turning off module %d\n", mod);
        controller_module_disable_power(mod);
        controller_module_disable_i2c(mod);
      }
  } else if (frame_type == CommandFrame) {
    if (message_type == EnergyQueryMessage) {

      signpost_energy_information_t info;

      int mod_num = signpost_api_addr_to_mod_num(source_address);

      info.energy_limit_mWh = (int)(signpost_energy_policy_get_module_energy_remaining_uwh(mod_num)/1000.0);
      info.energy_used_since_reset_mWh = (int)(signpost_energy_policy_get_module_energy_used_uwh(mod_num)/1000.0);
      info.time_since_reset_s = (int)(signpost_energy_policy_get_time_since_module_reset_ms(mod_num)/1000.0);
      info.energy_limit_warning_threshold = (info.energy_limit_mWh < 1000);
      info.energy_limit_critical_threshold = (info.energy_limit_mWh < 200);

      rc = signpost_energy_query_reply(source_address, &info);
      if (rc < 0) {
        printf(" - %d: Error sending energy query reply (code: %d). Replying with fail.\n", __LINE__, rc);
        signpost_api_error_reply_repeating(source_address, api_type, message_type, true, true, 1);
      }

    } else if (message_type == EnergyReportModuleConsumptionMessage) {
        //this is for the radio to report other module's energy usage
        printf("CALLBACK_ENERGY: Received energy report from 0x%.2x\n", source_address);

        //first we should get the message and unpack it
        signpost_energy_report_t report;
        memcpy(&report, message, message_length);

        //now send the report to the energy
        signpost_energy_policy_update_energy_from_report(signpost_api_addr_to_mod_num(source_address), &report);

        //reply to the report
        signpost_energy_report_reply(source_address, 1);
    } else if (message_type == EnergyResetMessage) {
        printf("CALLBACK_ENERGY: Received energy reset message from 0x%.2x\n", source_address);

        signpost_energy_policy_reset_module_energy_used(signpost_api_addr_to_mod_num(source_address));

        //reply
        signpost_energy_reset_reply(source_address, 1);
    } else {
      signpost_api_error_reply_repeating(source_address, api_type, message_type, true, true, 1);
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
    signpost_api_error_reply_repeating(source_address, api_type, message_type, true, true, 1);
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
        signpost_api_error_reply_repeating(source_address, api_type, message_type, true, true, 1);
      }

    } else if (message_type == TimeLocationGetLocationMessage) {
      signpost_timelocation_location_t location;
      memcpy(&location,&current_location,sizeof(signpost_timelocation_location_t));
      rc = signpost_timelocation_get_location_reply(source_address, &location);
      if (rc < 0) {
        printf(" - %d: Error sending TimeLocationGetLocationMessage reply (code: %d).\n", __LINE__, rc);
        signpost_api_error_reply_repeating(source_address, api_type, message_type, true, true, 1);
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
        } else if (module_state[i].isolation_state == ModuleEnabled) {
            module_state[i].isolation_state = ModuleEnabled;
            controller_module_enable_power(i);
            controller_module_enable_i2c(i);
        }
    }
}

static void signpost_controller_initialize_energy (void) {
    // Read FRAM to see if anything is stored there
    const unsigned FRAM_MAGIC_VALUE = 0x49A8000B;
    fm25cl_read_sync(0, sizeof(controller_fram_t));

    printf("Initializing energy\n");
    if (fram.magic == FRAM_MAGIC_VALUE) {
      // Great. We have saved data.
      // Initialize the energy algorithm with those values
      printf("Found saved energy data\n");
      signpost_energy_policy_init(&fram.remaining, &fram.used, &fram.time);

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

void app_watchdog_tickler(void) {
  app_watchdog_combine(WATCH_APP_TICKLE);
}

int signpost_controller_init (void) {
    // setup app watchdog
    app_watchdog_set_kernel_timeout(180000);
    app_watchdog_start();

    //configure FRAM
    printf("Configuring FRAM\n");
    fm25cl_set_read_buffer((uint8_t*) &fram, sizeof(controller_fram_t));
    fm25cl_set_write_buffer((uint8_t*) &fram, sizeof(controller_fram_t));

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
        printf(" - Error initializing as controller module with signpost library (code: %d)\n", rc);
        printf("   Sleeping 5s\n");
        delay_ms(5000);
      }
    } while (rc < 0);

    // Setup backplane by enabling the modules
    controller_init_module_switches();
    controller_all_modules_enable_power();
    controller_all_modules_enable_i2c();
    controller_all_modules_disable_usb();
    controller_gpio_enable_all_MODINs();
    controller_gpio_enable_all_MODOUTs(PullUp);
    controller_gpio_set_all();

    //setup timer callbacks to service the various signpost components
    static tock_timer_t energy_update_timer;
    timer_every(600000, update_energy_policy_cb, NULL, &energy_update_timer);

    static tock_timer_t check_init_timer;
    timer_every(1000, check_module_init_cb, NULL, &check_init_timer);

    static tock_timer_t check_watchdogs_timer;
    timer_every(60000, check_watchdogs_cb, NULL, &check_watchdogs_timer);

    return 0;
}
