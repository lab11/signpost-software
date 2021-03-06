#pragma once

#ifdef __cplusplus
#include <assert.h>
extern "C" {
#endif

#include <time.h>
#include <stdbool.h>
#include <stdint.h>
#include "signbus_app_layer.h"
#include "signbus_protocol_layer.h"

typedef void (*signpost_api_callback_t)(uint8_t source_address,
        signbus_frame_type_t frame_type, signbus_api_type_t api_type, uint8_t message_type,
        size_t message_length, uint8_t* message);

typedef struct api_handler {
    signbus_api_type_t       api_type;
    signpost_api_callback_t  callback;
} api_handler_t;

// Generic method to respond to any API Command with an Error
// Callers MUST echo back the api_type and message_type of the bad message.
__attribute__((warn_unused_result))
int signpost_api_error_reply(uint8_t destination_address,
        signbus_api_type_t api_type, uint8_t message_type, int error_code);

// Convenience method that will repeatedly try to reply with failures and
// itself "cannot" fail (it simply eventually gives up).
//
// The `print_warnings` and `print_on_first_send` parameters control whether
// this method prints to alert that a failure occured, with the former
// controlling all prints, and the latter designed to allow callers to simply
// directly call this method without needing to report failure themselves.
void signpost_api_error_reply_repeating(uint8_t destination_address,
        signbus_api_type_t api_type, uint8_t message_type, int error_code,
        bool print_warnings, bool print_on_first_send, unsigned tries);

// API layer send call
__attribute__((warn_unused_result))
int signpost_api_send(uint8_t destination_address,
                      signbus_frame_type_t frame_type,
                      signbus_api_type_t api_type,
                      uint8_t message_type,
                      size_t message_length,
                      uint8_t* message);

uint8_t* signpost_api_addr_to_key(uint8_t addr);
int signpost_api_addr_to_mod_num(uint8_t addr);
int signpost_api_appid_to_mod_num(uint16_t appid);
int signpost_api_module_name_to_mod_num(char* name);

// Revoke the key of a module
//
// params:
//  module_number: the module number corresponding to the key to revoke
//
// returns PORT_SUCCESS if module number is within number of modules on
// system (NUM_MODULES), and PORT_EINVAL otherwise
//
int signpost_api_revoke_key(uint8_t module_number);


/**************************************************************************/
/* INITIALIZATION API                                                     */
/**************************************************************************/

#if __cplusplus > 199711L
#define SIGNPOST_INITIALIZATION_NO_APIS nullptr
#else
#define SIGNPOST_INITIALIZATION_NO_APIS NULL
#endif

#define NUM_MODULES 8
#define NAME_LEN 16

typedef struct module_struct {
    uint32_t                magic;
    uint8_t                 self_mod_num;
    uint8_t                 i2c_address;
    char                    self_name[NAME_LEN];
    char                    names[NUM_MODULES][NAME_LEN];
    uint8_t                 i2c_address_mods[NUM_MODULES];
    uint16_t                nonces[NUM_MODULES];
    bool                    haskey[NUM_MODULES];
    uint8_t                 keys[NUM_MODULES][ECDH_KEY_LENGTH];
} module_state_t;

typedef enum initialization_state {
    RequestIsolation = 0,
    Declare,
    KeyExchange,
    FinishExchange,
    Done,
    CheckKeys,
    //RegisterWithKeys,
    //ConfirmChallenge,
} initialization_state_t;

typedef enum initialization_message_type {
   InitializationDeclare = 0,
   InitializationKeyExchange,
   InitializationGetMods,
   //InitializationRegister,
   InitializationRevoke,
   InitializationGetState,
} initialization_message_type_t;

typedef enum module_address {
    ModuleAddressController = 0x20,
    ModuleAddressStorage = 0x21,
    ModuleAddressRadio = 0x22,
} module_address_t;

//Basic initialization for most sensor modules
__attribute__((warn_unused_result))
int signpost_init(const char* org_name, const char* module_name);

// Initialize this module.
// Must be called before any other signpost API methods.
//
// params:
//  i2c_address  - The I2C address of this calling module
//  api_handlers - Array of signpost APIs that this calling module implements
//                 The final element of this array MUST be NULL.
//                 This array MUST be static (pointer must be valid forever).
//                 Modules that implement no APIs MUST pass SIGNPOST_INITIALIZATION_NO_APIS.
__attribute__((warn_unused_result))
int signpost_initialization_module_init(const char* org_name, const char* module_name, uint8_t i2c_address, api_handler_t** api_handlers);

// A special initialization routine for the controller module only.
__attribute__((warn_unused_result))
int signpost_initialization_controller_module_init(api_handler_t** api_handlers);

// Request isolation with the controller
// TODO add parameter for which modules to isolate
int signpost_initialization_request_isolation(void);

// Initialize with another module
// Will first arrange isolation between controller and other module,
// then complete a key exchange with the module
//
// params:
//  module_address: i2c address of module to initialize with
int signpost_initialization_initialize_with_module(uint8_t module_address);

// Send a exchange request to another module
// Assumes controller has already isolated source and target
//
// params:
//  destination_address - The I2C address of the module to exchange keys with
//__attribute__((warn_unused_result))
//int signpost_initialization_key_exchange_send(uint8_t destination_address);

// Send a response to a registration request if module key already stored
//
// params:
//  source_address  - The I2C address of the module that sent a registration
//  request
//  TODO this should be replaced with appid eventually
__attribute__((warn_unused_result))
int signpost_initialization_register_respond(uint8_t source_address);

// Send a response to a declare request
// Assumes controller has already isolated source
//
// params:
//  source_address  - The I2C address of the module that sent a declare request
//  module_number   - The module slot that is currently isolated
__attribute__((warn_unused_result))
int signpost_initialization_declare_respond(uint8_t source_address, uint8_t new_address, uint8_t module_number, char* name);

// Send a response to a key exchange request
// Assumes controller has already isolated source and target
//
// params:
//  source_address  - The I2C address of the module that sent a key exchange request
//  ecdh_params     - The buffer of ecdh params sent in the InitializationKeyExchange message
//  len             - The length of data in ecdh_params
__attribute__((warn_unused_result))
int signpost_initialization_key_exchange_respond(uint8_t source_address, uint8_t* ecdh_params, size_t len);

// Get a digest of modules from the controller
//
// params:
__attribute__((warn_unused_result))
int signpost_initialization_get_module_state(void);
int signpost_initialization_get_module_state_reply(uint8_t address);

/**************************************************************************/
/* STORAGE API                                                            */
/**************************************************************************/

#define STORAGE_LOG_LEN 32

enum storage_message_type {
   StorageWriteMessage = 0,
   StorageReadMessage= 1,
   StorageDeleteMessage= 2,
   StorageScanMessage= 3,
};

typedef struct {
  char logname[STORAGE_LOG_LEN+1]; //+1 for null char
  size_t offset;
  size_t length;
} Storage_Record_t;

// Write data to the Storage Master
//
// params:
//  record_list     - List of records to fill
//  list_len        - Actual number of records filled
//  max_list_len    - Maximum number of records to accept
__attribute__((warn_unused_result))
int signpost_storage_scan (Storage_Record_t* record_list, size_t* list_len);

// Write data to the Storage Master
//
// params:
//  data            - Data to write
//  len             - Length of data
//  record_pointer  - Pointer to record that will indicate location of written data
__attribute__((warn_unused_result))
int signpost_storage_write (uint8_t* data, size_t len, Storage_Record_t* record_pointer);

// Read data from the Storage Master
//
// params:
//  data            - Pointer to buffer to read to
//  len             - Length of data to read
//  record_pointer  - Record that will indicate location of stored data
__attribute__((warn_unused_result))
int signpost_storage_read (uint8_t* data, size_t *len, Storage_Record_t* record_pointer);

// Delete log from the Storage Master
//
// params:
//  record_pointer  - Record that will indicate location of data to delete
__attribute__((warn_unused_result))
int signpost_storage_delete (Storage_Record_t* record_pointer);

// Storage master response to scan request
//
// params:
//  destination_address - Address to reply to
//  list                - Buffer list of file names
//  list_len            - Max list buffer length
__attribute__((warn_unused_result))
int signpost_storage_scan_reply(uint8_t destination_address, Storage_Record_t* list, size_t list_len);

// Storage master response to write request
//
// params:
//  destination_address - Address to reply to
//  record_pointer      - Data at record
__attribute__((warn_unused_result))
int signpost_storage_write_reply (uint8_t destination_address, Storage_Record_t* record_pointer);

// Storage master response to read request
//
// params:
//  destination_address - Address to reply to
//  data                - buffer to write read data
//  length              - length of read data
__attribute__((warn_unused_result))
int signpost_storage_read_reply (uint8_t destination_address, uint8_t* data, size_t length);

// Storage master response to delete request
//
// params:
//  destination_address - Address to reply to
//  record_pointer      - Returned record
__attribute__((warn_unused_result))
int signpost_storage_delete_reply (uint8_t destination_address, Storage_Record_t* record_pointer);

/**************************************************************************/
/* NETWORKING API                                                         */
/**************************************************************************/
enum networking_message_type {
    NetworkingPublishMessage = 0,
    NetworkingSubscribeMessage,
};

typedef void (*signpost_networking_subscribe_cb_t)(char* topic, uint8_t* data, uint8_t data_len);

//used by other modules
__attribute__((warn_unused_result))
int signpost_networking_publish(const char* topic, uint8_t* data, uint8_t data_len);
int signpost_networking_subscribe(signpost_networking_subscribe_cb_t cb);

//Used by the radio module
int signpost_networking_publish_reply(uint8_t src_addr, int return_code);
int signpost_networking_subscribe_send(uint8_t dest_addr, char* topic, uint8_t* data, uint8_t data_len);

/**************************************************************************/
/* PROCESSING API                                                         */
/**************************************************************************/

enum processing_return_type {
    ProcessingSuccess = 0,
    ProcessingNotExist = 1,
    ProcessingSizeError = 2,
    ProcessingCRCError = 3,
};

enum processing_message_type {
    ProcessingInitMessage = 0,
    ProcessingOneWayMessage = 1,
    ProcessingTwoWayMessage = 2,
    ProcessingEdisonReadMessage = 3,
    ProcessingEdisonResponseMessage = 4,
};

// Initialize, provide the path to the python module used by the RPC
//
// params:
//  path    - linux-style path to location of python module to handle this
//  modules rpcs (e.g. /path/to/python/module.py)
__attribute__((warn_unused_result))
int signpost_processing_init(const char* path);

// Send an RPC with no expected response
//
// params:
//  buf - buffer containing RPC to send
//  len - length of buf
__attribute__((warn_unused_result))
int signpost_processing_oneway_send(uint8_t* buf, uint16_t len);

// Send an RPC with an expected response
//
// params:
//  buf - buffer containing RPC to send
//  len - length of buf
__attribute__((warn_unused_result))
int signpost_processing_twoway_send(uint8_t* buf, uint16_t len);

// Receive RPC response
//
// params:
//  buf - buffer to store result
//  len - length of buf
__attribute__((warn_unused_result))
int signpost_processing_twoway_receive(uint8_t* buf, uint16_t* len);

// Reply from Storage Master to RPC requesting module
//
// params:
//  src_addr        - address of module that originally requested the RPC
//  message_type    - type of RPC message
//  response        - RPC response from compute resource
//  response_len    - len of response
__attribute__((warn_unused_result))
int signpost_processing_reply(uint8_t src_addr, uint8_t message_type, uint8_t* response, uint16_t response_len);

/**************************************************************************/
/* ENERGY API                                                             */
/**************************************************************************/

enum energy_message_type {
    EnergyQueryMessage = 0,
    EnergyResetMessage = 1,
    EnergyReportModuleConsumptionMessage = 2,
    EnergyDutyCycleMessage = 3,
};

//information sent to a module from the controller
typedef struct __attribute__((packed)) energy_information {
    uint32_t    energy_used_since_reset_uWh;
    uint32_t    energy_limit_uWh;
    uint32_t    time_since_reset_s;
    uint8_t     energy_limit_warning_threshold;
    uint8_t     energy_limit_critical_threshold;
} signpost_energy_information_t;

#ifdef __cplusplus //{}
static_assert(sizeof(signpost_energy_information_t) == 14, "On-wire structure size");
#else
_Static_assert(sizeof(signpost_energy_information_t) == 14, "On-wire structure size");
#endif

//a mechanism for modules to report energy usage from other modules
//For instance this allows the radio to tell the controller some
//of its energy was used when providing a service to other modules
typedef struct __attribute__((packed)) energy_report_module {
    uint16_t application_id; //the application identifier that used the energy
    uint32_t energy_used_uWh; //an integer number of the energy used in uWh
} signpost_energy_report_module_t;

//we make an array of them to report full usage
typedef struct __attribute__((packed)) energy_report {
    uint8_t num_reports;
    signpost_energy_report_module_t* reports;
} signpost_energy_report_t;

// Query the controller for energy information
//
// params:
//  energy  - an energy_information_t struct to fill
__attribute__((warn_unused_result))
int signpost_energy_query(signpost_energy_information_t* energy);

// Reset the energy book-keeping for your module
//
// params: none
__attribute__((warn_unused_result))
int signpost_energy_reset(void);

// Tell the controller to turn me off then on again in X time
// params:
//  time - time in milliseconds to turn on again
int signpost_energy_duty_cycle(uint32_t time_ms);


// Tell the controller about modules who have used energy
// params: a struct of module addresses and energy percents of yours they have used
// This will distribute energy since the last report to the modules that have used
// that energy.
int signpost_energy_report(signpost_energy_report_t* report);

// Query the controller for energy information, asynchronously
//
// params:
//  energy  - an energy_information_t struct to fill
//  cb      - the callback to call when energy information is collected
__attribute__((warn_unused_result))
int signpost_energy_query_async(signpost_energy_information_t* energy, signbus_app_callback_t cb);

// Response from the controller to the requesting module
//
// params:
//  destination_address -   requesting address for this energy information
//  info                -   energy information
__attribute__((warn_unused_result))
int signpost_energy_query_reply(uint8_t destination_address, signpost_energy_information_t* info);

// Response from controller to requesting module
// returns a signpost error define.
int signpost_energy_report_reply(uint8_t destination_address, int return_code);

// Response from controller to requesting module
// returns a signpost error define.
int signpost_energy_reset_reply(uint8_t destination_address, int return_code);

/**************************************************************************/
/* TIME & LOCATION API                                                    */
/**************************************************************************/

typedef enum {
    TimeLocationGetTimeMessage = 0,
    TimeLocationGetLocationMessage = 1,
    TimeLocationGetTimeNextPpsMessage = 2,
} signpost_timelocation_message_type_e;

typedef struct __attribute__((packed)) {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hours;
    uint8_t  minutes;
    uint8_t  seconds;
    uint8_t  satellite_count;
} signpost_timelocation_time_t;

typedef struct __attribute__((packed)) {
    uint32_t latitude;  // Latitude in microdegrees (divide by 10^6 to get degrees)
    uint32_t longitude; // Longitude in microdegrees
    uint8_t  satellite_count;
} signpost_timelocation_location_t;

// Get time from controller
//
// params:
//  time     - signpost_timelocation_time_t struct to fill
__attribute__((warn_unused_result))
int signpost_timelocation_get_time(time_t* time);

// Get location from controller
//
// params:
//  location - signpost_location_time_t struct to fill
__attribute__((warn_unused_result))
int signpost_timelocation_get_location(signpost_timelocation_location_t* location);

// Controller reply to time requesting module
//
// params:
//
//  destination_address - i2c address of requesting module
//  time                - signpost_timelocation_time_t struct to return
__attribute__((warn_unused_result))
int signpost_timelocation_get_time_reply(uint8_t destination_address, signpost_timelocation_time_t* time);

// Controller reply to location requesting module
//
// params:
//
//  destination_address - i2c address of requesting module
//  location            - signpost_timelocation_location_t struct to return
__attribute__((warn_unused_result))
int signpost_timelocation_get_location_reply(uint8_t destination_address, signpost_timelocation_location_t* location);

/**************************************************************************/
/* WATCHDOG API                                                           */
/**************************************************************************/

typedef enum {
    WatchdogStartMessage = 0,
    WatchdogTickleMessage = 1,
    WatchdogResponseMessage = 2,
} signpost_watchdog_message_type_e;

int signpost_watchdog_start(void);
int signpost_watchdog_tickle(void);
int signpost_watchdog_reply(uint8_t destination_address);

/**************************************************************************/
/* EDISON API                                                             */
/**************************************************************************/

typedef enum {
    EdisonReadHandleMessage = 0,
    EdisonReadRPCMessage = 1,
} signpost_edison_message_type_e;

#ifdef __cplusplus
}
#endif
