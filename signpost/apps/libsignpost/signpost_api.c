#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "CRC16.h"
#include "signbus_app_layer.h"
#include "signbus_io_interface.h"
#include "signpost_api.h"
#include "port_signpost.h"
#include "signpost_entropy.h"

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/ecp.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstack-usage="
#if CC_VERSION_MAJOR >= 7
#pragma GCC diagnostic ignored "-Wformat-truncation="
#endif

#define NAME_LENGTH 16
#define NUM_MODULES 5

struct module_api_struct {
    api_handler_t**         api_handlers;
} module_api = {0};


typedef struct module_info_struct {
    uint8_t         address;
    uint8_t         module_number;
    char            name[NAME_LENGTH];
    bool            valid;
    uint8_t         key[KEY_LENGTH];
} module_info_t;

typedef struct module_state_struct {
    uint8_t         address;
    char            name[NAME_LENGTH];
    bool            valid;
    uint8_t         key[KEY_LENGTH];
} module_state_t;

typedef struct system_state_struct {
    bool            valid;
    uint8_t         system_key[KEY_LENGTH];
    module_info_t   modules[NUM_MODULES];
} system_state_t;

static module_state_t module_state;
static system_state_t system_state;

//Helper functions that can be used by the Signpost Controller
uint8_t* signpost_api_addr_to_key(uint8_t addr) {

    if(addr == ControlModuleAddress || addr == RadioModuleAddress || addr == StorageModuleAddress) {
        if(module_state.valid) {
            return module_state.key;
        } else {
            return NULL;
        }
    } else{
        for(uint8_t i = 0; i < NUM_MODULES; i++) {
            if(addr == system_state.modules[i].address && system_state.modules[i].valid) {
                return system_state.modules[i].key;
            }
        }
        return NULL;
    }
}

int signpost_api_addr_to_mod_num(uint8_t addr){
    switch(addr) {
    case ControlModuleAddress:
        return 3;
    break;
    case RadioModuleAddress:
        return 0;
    break;
    case StorageModuleAddress:
        return 3;
    break;
    default:
        for(uint8_t i = 0; i < NUM_MODULES; i++) {
            if(addr == system_state.modules[i].address) {
                return system_state.modules[i].module_number;
            }
        }
    break;
    }

    port_printf("WARN: Do not have module registered to address 0x%x\n", addr);
    return PORT_FAIL;
}

uint8_t signpost_api_appid_to_mod_num(uint16_t appid) {
    return signpost_api_addr_to_mod_num(appid);
}

int signpost_api_error_reply(uint8_t destination_address,
        signbus_api_type_t api_type, uint8_t message_type, int error_code) {
    return signpost_api_send(destination_address,
            ErrorFrame, api_type, message_type, sizeof(int), (uint8_t*)&error_code);
}

void signpost_api_error_reply_repeating(uint8_t destination_address,
        signbus_api_type_t api_type, uint8_t message_type, int error_code,
        bool print_warnings, bool print_on_first_send, unsigned tries) {
   int rc;
   if (print_warnings && print_on_first_send) {
      port_printf(" - Sending API Error reply to 0x%02x for api 0x%02x and message 0x%02x.\n",
            destination_address, api_type, message_type);
   }
   do {
      rc = signpost_api_error_reply(destination_address, api_type, message_type, error_code);
      if (rc < 0) {
         tries--;
         port_printf(" - Error sending API Error reply to 0x%02x (code: %d).\n",
               destination_address, rc);
         port_printf(" - Sleeping 1s. Tries remaining %d\n", tries);
         port_signpost_delay_ms(1000);
      }
   } while ( (tries > 0) && (rc < 0) );
}

int signpost_api_send(uint8_t destination_address,
                      signbus_frame_type_t frame_type,
                      signbus_api_type_t api_type,
                      uint8_t message_type,
                      size_t message_length,
                      uint8_t* message) {

    return signbus_app_send(destination_address, signpost_api_addr_to_key, frame_type, api_type,
                            message_type, message_length, message);
}

/**************************************************************************/
/* INCOMING MESSAGE / ASYNCHRONOUS DISPATCH                               */
/**************************************************************************/

// We can only have one active receive call at a time, which means providing
// a synchronous-looking interface can be a little tricky, because we could
// be waiting for a synchronous reply when another module sends us a message

// To handle this, we have a single, shared receive mechanism that is always
// issuing an asynchronous receive.

#define INCOMING_MESSAGE_BUFFER_LENGTH 1024
static uint8_t               incoming_source_address;
static signbus_frame_type_t  incoming_frame_type;
static signbus_api_type_t    incoming_api_type;
static uint8_t               incoming_message_type;
static size_t                incoming_message_length;
static uint8_t*              incoming_message;
static uint8_t               incoming_message_buffer[INCOMING_MESSAGE_BUFFER_LENGTH];

// See comment in protocol_layer.h
static uint8_t               incoming_protocol_buffer[INCOMING_MESSAGE_BUFFER_LENGTH];

static signbus_app_callback_t* incoming_active_callback = NULL;



// Forward decl
static void signpost_api_recv_callback(int len_or_rc);

static void signpost_api_start_new_async_recv(void) {
    int rc = signbus_app_recv_async(signpost_api_recv_callback,
            &incoming_source_address, signpost_api_addr_to_key,
            &incoming_frame_type, &incoming_api_type,
            &incoming_message_type, &incoming_message_length, &incoming_message,
            INCOMING_MESSAGE_BUFFER_LENGTH, incoming_message_buffer);
    if (rc != 0) {
        port_printf("%s:%d UNKNOWN ERROR %d\n", __FILE__, __LINE__, rc);
        port_printf("*** NO MORE MESSAGES WILL BE RECEIVED ***\n");
    }
}

static void signpost_api_recv_callback(int len_or_rc) {
    if (len_or_rc < 0) {
        if (len_or_rc == PORT_ECRYPT) {
            port_printf("Dropping message with HMAC/HASH failure\n");
            // Unable to decrypt this message, we revoke our useless key
            //signpost_api_send(incoming_source_address, CommandFrame, InitializationApiType, InitializationRevoke, sizeof(int), (uint8_t*)&len_or_rc);

            signpost_api_start_new_async_recv();
            return;
        } else {
            port_printf("%s:%d It's all fubar?\n", __FILE__, __LINE__);
            // XXX trip watchdog reset or s/t?
            //volatile int crash = *(int*)0;
        }
    }
    if ( (incoming_frame_type == NotificationFrame) || (incoming_frame_type == CommandFrame) ) {
        //For generic API handlers call the handler specified by the module (radio, storage, etc)
        api_handler_t** handler = module_api.api_handlers;
        while (*handler != NULL) {
            if ((*handler)->api_type == incoming_api_type) {
                (*handler)->callback(incoming_source_address,
                        incoming_frame_type, incoming_api_type, incoming_message_type,
                        incoming_message_length, incoming_message);
                break;
            }
            handler++;
        }
        //If we haven't specified an init
        if(incoming_api_type
        if (handler == NULL) {
            port_printf("Warn: Unsolicited message for api %d. Dropping\n", incoming_api_type);
        }
    } else if ( (incoming_frame_type == ResponseFrame) || (incoming_frame_type == ErrorFrame) ) {
        if (incoming_active_callback != NULL) {
            // clear this before passing it on
            signbus_app_callback_t* temp = incoming_active_callback;
            incoming_active_callback = NULL;
            temp(len_or_rc);
        } else {
            port_printf("Warn: Unsolicited response/error. Dropping\n");
        }
    } else {
        port_printf("Invalid frame type: %d. Dropping message\n", incoming_frame_type);
    }

    signpost_api_start_new_async_recv();
}


/**************************************************************************/
/* INITIALIZATION API                                                     */
/**************************************************************************/

// state machine
static initialization_state_t init_state;

// waiting variables
static bool request_isolation_complete;
static bool declare_complete;
static bool verify_complete;

// state of isolation
static bool is_isolated = 0;

// module address currently initializing with
// first initialization is with controller
static uint8_t mod_addr_init = ControlModuleAddress;

/**************************************/
/* Initialization Callbacks           */
/**************************************/
static void signpost_initialization_declare_callback(int len_or_rc __attribute__ ((unused))) {
    // Flip waiting variable, change state if well-formed message
    declare_complete = true;
}

static void signpost_initialization_isolation_callback(int unused __attribute__ ((unused))) {
    is_isolated = 1;

    // update state and flip waiting variable
    request_isolation_complete = true;
}

static void signpost_initialization_lost_isolation_callback(int unused __attribute__ ((unused))) {
    // reset state to request isolation if no longer isolated
    is_isolated = 0;
    if (init_state != Done)
      init_state = RequestIsolation;
}

//This callback handles key updates for the system modules that are received
//out of band.
void system_initialization_handler(uint8_t source_address,
        signbus_frame_type_t frame_type, __attribute ((unused)) signbus_api_type_t api_type,
        uint8_t message_type, size_t message_length, uint8_t* message) {

    if(frame_type == CommandFrame) {
        if(message_type == InitializationSystemDeclare) {
            //This is a system state update message - update the state
            memcpy(system_state,message,sizeof(system_state_t));

            //respond with a success ack
            int response = PORT_SUCCESS;
            signpost_api_send(ControlModuleAddress,ResponseFrame,
                    InitializationApiType,InitializationSystemDeclare,sizeof(int),&response);
        }
    }

}
/**************************************/
/* Initialization Helper Functions    */
/**************************************/


static int signpost_initialization_request_isolation(void) {
    int rc;
    // Pull Mod_Out Low to signal controller
    // Wait on controller interrupt on MOD_IN
    rc = port_signpost_mod_out_clear();
    if (rc != PORT_SUCCESS) return rc;
    rc = port_signpost_debug_led_on();
    if (rc != PORT_SUCCESS) return rc;

    return PORT_SUCCESS;
}

static int signpost_initialization_declare(void) {
    // set callback for handling response from controller/modules
    if (incoming_active_callback != NULL && incoming_active_callback != signpost_initialization_declare_callback) {
        return PORT_EBUSY;
    }

    incoming_active_callback = signpost_initialization_declare_callback;

    //Send a declare message with the module name
    if (signpost_api_send(ControlModuleAddress, CommandFrame,
          InitializationApiType, InitializationDeclare,
          strnlen(module_state.name,NAME_LENGTH),(uint8_t*)(module_state.name)) >= PORT_SUCCESS) {
        return PORT_SUCCESS;
    }

    return PORT_FAIL;
}

static int signpost_initialization_system_declare(void) {
    // set callback for handling response from controller/modules
    if (incoming_active_callback != NULL && incoming_active_callback != signpost_initialization_declare_callback) {
        return PORT_EBUSY;
    }

    incoming_active_callback = signpost_initialization_declare_callback;

    //Send a declare message with the module name
    if (signpost_api_send(ControlModuleAddress, CommandFrame,
          InitializationApiType, InitializationSystemDeclare,
          0,NULL) >= PORT_SUCCESS) {
        return PORT_SUCCESS;
    }

    return PORT_FAIL;
}

int signpost_initialization_system_state_update(uint8_t address) {
    //send the system state struct to the radio/storage master
    return signpost_api_send(source_address, ResponseFrame, InitializationApiType,
            InitializationSystemDeclare, sizeof(system_state_t), &system_state);
}


int signpost_initialization_declare_respond(module_state_t* state) {
    return signpost_api_send(0x00, ResponseFrame, InitializationApiType, InitializationDeclare, sizeof(module_state_t), state);
}

static int signpost_initialize_common(uint8_t i2c_address) {
    int rc;

    // Initialize the lower layers
    signbus_io_init(i2c_address);
    rc = signpost_entropy_init();
    if (rc < 0) return rc;
    // See comment in protocol_layer.h
    signbus_protocol_setup_async(incoming_protocol_buffer, INCOMING_MESSAGE_BUFFER_LENGTH);

    signpost_api_start_new_async_recv();

    // Initialize the hardware
    port_signpost_mod_out_set();
    port_signpost_debug_led_off();

    // setup interrupts for changes in isolated state
    rc = port_signpost_mod_in_enable_interrupt_rising(signpost_initialization_lost_isolation_callback);
    if (rc != PORT_SUCCESS) return rc;

    rc = port_signpost_mod_in_enable_interrupt_falling(signpost_initialization_isolation_callback);
    if (rc != PORT_SUCCESS) return rc;

    return PORT_SUCCESS;
}

int signpost_initialization_controller_module_init(api_handler_t** api_handlers) {
    //module_state_t check_state;
    int rc = signpost_initialization_common(ControlModuleAddress, api_handlers);
    if (rc < 0) return rc;

    //port_signpost_load_state(&check_state);
    //printf("magic: 0x%lx\n", check_state.magic);
    //if (check_state.magic == MOD_STATE_MAGIC) {
    //  memcpy(&module_info, &check_state, sizeof(module_state_t));
    //}

    // Begin listening for replies
    signpost_api_start_new_async_recv();

    SIGNBUS_DEBUG("complete\n");
    return PORT_SUCCESS;
}

static int signpost_system_initialize_loop(void) {
    int rc;

    while(1) {
        switch(init_state) {
          case RequestIsolation:
            request_isolation_complete = false;
            rc = signpost_initialization_request_isolation();
            if (rc != PORT_SUCCESS) {
              break;
            }

            port_printf("INIT: Requested I2C isolation with controller\n");
            rc = port_signpost_wait_for_with_timeout(&request_isolation_complete, 5000);
            if (rc == PORT_FAIL) {
              port_printf("INIT: Timed out waiting for controller isolation\n");
              port_signpost_mod_out_set();
              port_signpost_delay_ms(3000);
              init_state = RequestIsolation;
            };

            init_state = Declare;
            break;
          case Declare:
            // check that mod_in is still low after delay
            port_signpost_delay_ms(50);
            if(!is_isolated && port_signpost_mod_in_read() != 0) {
              init_state = RequestIsolation;
              break;
            }
            port_printf("INIT: Granted I2C isolation and started initialization with Controller\n");

            // Now declare self to controller
            declare_complete = false;
            rc = signpost_initialization_system_declare();
            if (rc != PORT_SUCCESS) {
              init_state = RequestIsolation;
              break;
            }

            rc = port_signpost_wait_for_with_timeout(&declare_complete, 2000);
            if (rc == PORT_FAIL) {
              port_printf("INIT: Timed out waiting for controller declare response\n");
              init_state = RequestIsolation;
            };

            //put the incoming message in a declare response struct
            memcpy(&system_state,incoming_message,sizeof(system_state_t));

            //break to the verify step
            init_state = Done;
            break;
          case Done:
            // Save module state
            //port_signpost_save_state(&module_info);
            // Completed Init
            port_signpost_mod_out_set();
            port_signpost_debug_led_off();
            port_printf("INIT: Completed Initialization with Controller\n");
            return PORT_SUCCESS;

          default:
            break;
        }
    }
    return PORT_SUCCESS;
}



static int signpost_initialize_loop(void) {
    int rc;

    while(1) {
        switch(init_state) {
          case RequestIsolation:
            request_isolation_complete = false;
            rc = signpost_initialization_request_isolation();
            if (rc != PORT_SUCCESS) {
              break;
            }

            port_printf("INIT: Requested I2C isolation with controller\n");
            rc = port_signpost_wait_for_with_timeout(&request_isolation_complete, 5000);
            if (rc == PORT_FAIL) {
              port_printf("INIT: Timed out waiting for controller isolation\n");
              port_signpost_mod_out_set();
              port_signpost_delay_ms(3000);
              init_state = RequestIsolation;
            };

            init_state = Declare;
            break;
          case Declare:
            // check that mod_in is still low after delay
            port_signpost_delay_ms(50);
            if(!is_isolated && port_signpost_mod_in_read() != 0) {
              init_state = RequestIsolation;
              break;
            }
            port_printf("INIT: Granted I2C isolation and started initialization with Controller\n");

            // Now declare self to controller
            declare_complete = false;
            rc = signpost_initialization_declare();
            if (rc != PORT_SUCCESS) {
              init_state = RequestIsolation;
              break;
            }

            rc = port_signpost_wait_for_with_timeout(&declare_complete, 2000);
            if (rc == PORT_FAIL) {
              port_printf("INIT: Timed out waiting for controller declare response\n");
              init_state = RequestIsolation;
            };

            //put the incoming message in a declare response struct
            memcpy(&module_state,incoming_message,sizeof(module_state_t));

            //set the keys received from the control module
            memcpy(module_state.key,r.key,KEY_LENGTH);
            module_state.valid = 1;

            module_state.address = r.address;

            //reset everything with the correct I2C address
            signpost_initialize_common(module_state.address);

            //break to the verify step
            init_state = Done;
            break;
          case Done:
            // Save module state
            //port_signpost_save_state(&module_info);
            // Completed Init
            port_signpost_mod_out_set();
            port_signpost_debug_led_off();
            port_printf("INIT: Completed Initialization with Controller\n");
            return PORT_SUCCESS;

          default:
            break;
        }
    }
    return PORT_SUCCESS;

}


int signpost_initialize_control_module(api_handler_t** api_handlers) {
    int rc;

    module_api.api_handlers = api_handlers;

    // Initialize the lower layers
    signbus_io_init(ControlModuleAddress);
    rc = signpost_entropy_init();
    if (rc < 0) return rc;
    // See comment in protocol_layer.h
    signbus_protocol_setup_async(incoming_protocol_buffer, INCOMING_MESSAGE_BUFFER_LENGTH);

    signpost_api_start_new_async_recv();

    return PORT_SUCCESS;
}

int signpost_initialize_radio_module(api_handler_t** api_handlers) {
    //find number of handlers
    int i = 0;
    for(;;i++){
        if(api_handlers[i] == NULL)
            break;
    }

    //this is how big the handlers array should be
    i += 2;

    handlers = malloc(api_handler_t*, i);
    int j = 0;
    for(; j < i-1; j++) {
        memcpy(handlers[j],api_handlers[j],sizeof(api_handler_t));
    }

    //create an implicit init handler
    api_handler_t init_handler = {InitializationApiType, system_initialization_callback};
    memcpy(handlers[j],init_handler,sizeof(api_handler_t));
    j++;
    handlers[j] = NULL;

    // set the api_handlers to this new handler array
    module_api.api_handlers = handlers;

    signpost_initialize_common(RadioModuleAddress);

    return signpost_system_initialize_loop();
}

int signpost_initialize_storage_module(api_handler_t** api_handlers) {
    //find number of handlers
    int i = 0;
    for(;;i++){
        if(api_handlers[i] == NULL)
            break;
    }

    //this is how big the handlers array should be
    i += 2;

    handlers = malloc(api_handler_t*, i);
    int j = 0;
    for(; j < i-1; j++) {
        memcpy(handlers[j],api_handlers[j],sizeof(api_handler_t));
    }

    //create an implicit init handler
    api_handler_t init_handler = {InitializationApiType, system_initialization_callback};
    memcpy(handlers[j],init_handler,sizeof(api_handler_t));
    j++;
    handlers[j] = NULL;

    // set the api_handlers to this new handler array
    module_api.api_handlers = handlers;


    signpost_initialize_common(StorageModuleAddress);

    return signpost_system_initialize_loop();
}



int signpost_initialize(char* module_name) {
    //attempt to load the module state from memory
    int rc = port_signpost_load_state((uint8_t*)&module_state, sizeof(module_state_t));
    if(rc != PORT_SUCCESS) return rc;

    //Check if the loaded memory matches the initialized name
    if(!strncmp(module_state.name, module_name, NAME_LENGTH)) {

        //check if the key is valid
        if(module_state.valid) {

            //great it matches, check with the controller to see if its valid
            rc = signpost_initialize_common(module_state.address);
            if(rc != PORT_SUCCESS) return rc;

            //Go into the Verify Initialization State to check settings
            init_state = Done;

            return signpost_initialize_loop();
        }
    }

    //it does not match - start the initialization process from scratch
    rc = signpost_initialize_common(0x00);
    if(rc != PORT_SUCCESS) return rc;

    //set the state struct valid bits to zero
    module_state.valid = false;

    //copy the module name into the state structure
    strncpy(module_state.name,module_name,strnlen(module_name,NAME_LENGTH));

    //Request Isolation to start the initialization Process
    init_state = RequestIsolation;

    return signpost_initialize_loop();
}

int signpost_initialization_initialize_with_module(uint8_t module_address) {
    init_state = RequestIsolation;
    mod_addr_init = module_address;
    return signpost_initialization_initialize_loop();
}

/**************************************************************************/
/* STORAGE API                                                            */
/**************************************************************************/

// message response state
static bool storage_ready;
static bool storage_result;
static Storage_Record_t* callback_record = NULL;
static uint8_t* callback_data = NULL;
static size_t* callback_length = NULL;

static void signpost_storage_scan_callback(int len_or_rc) {
    if (len_or_rc < PORT_SUCCESS) {
        // error code response
        storage_result = len_or_rc;
    } else if ((size_t) len_or_rc > *callback_length * sizeof(Storage_Record_t)) {
        // invalid response length
        port_printf("%s:%d - Error: bad len, got %d, want %d\n",
                __FILE__, __LINE__, len_or_rc, *callback_length*sizeof(Storage_Record_t));
        storage_result = PORT_FAIL;
    } else {
        // valid storage record
        if (callback_record != NULL && callback_length != NULL) {
            // copy over record response
            *callback_length = len_or_rc / sizeof(Storage_Record_t);
            printf("%u\n", *callback_length);
            memcpy(callback_record, incoming_message, len_or_rc);
        }
        callback_record = NULL;
        callback_length = NULL;
        storage_result = PORT_SUCCESS;
    }

    // response received
    storage_ready = true;
}

static void signpost_storage_write_callback(int len_or_rc) {
    if (len_or_rc < PORT_SUCCESS) {
        // error code response
        storage_result = len_or_rc;
    } else if (len_or_rc != sizeof(Storage_Record_t)) {
        // invalid response length
        port_printf("%s:%d - Error: bad len, got %d, want %d\n",
                __FILE__, __LINE__, len_or_rc, sizeof(Storage_Record_t));
        storage_result = PORT_FAIL;
    } else {
        // valid storage record
        if (callback_record != NULL) {
            // copy over record response
            memcpy(callback_record, incoming_message, len_or_rc);
        }
        callback_record = NULL;
        storage_result = PORT_SUCCESS;
    }

    // response received
    storage_ready = true;
}

static void signpost_storage_read_callback(int len_or_rc) {
    if (len_or_rc < PORT_SUCCESS) {
        // error code response
        storage_result = len_or_rc;
    } else if (callback_length == NULL) {
        storage_result = PORT_EINVAL;
    } else if ((size_t) len_or_rc > *callback_length) {
        // invalid response length
        port_printf("%s:%d - Error: bad len, got %d, want %d\n",
                __FILE__, __LINE__, len_or_rc, *callback_length);
        storage_result = PORT_FAIL;
    } else {
        // valid data
        if (callback_data != NULL) {
            // copy over record response
            memcpy(callback_data, incoming_message, *callback_length);
        }
        callback_data= NULL;
        callback_length = NULL;
        storage_result = PORT_SUCCESS;
    }

    // response received
    storage_ready = true;
}

int signpost_storage_scan (Storage_Record_t* record_list, size_t* list_len) {
    storage_ready = false;
    storage_result = PORT_SUCCESS;
    callback_record = record_list;
    callback_length = list_len;

    // set up callback
    if (incoming_active_callback != NULL) {
        return PORT_EBUSY;
    }
    incoming_active_callback = signpost_storage_scan_callback;

    // send message
    int err = signpost_api_send(StorageModuleAddress, CommandFrame,
            StorageApiType, StorageScanMessage, sizeof(*list_len), (uint8_t*) list_len);

    if (err < PORT_SUCCESS) {
        storage_ready = true;
        incoming_active_callback = NULL;
        return err;
    }

    // wait for response
    port_signpost_wait_for_with_timeout(&storage_ready, 5000);
    if (err != 0) {
      storage_ready = true;
      incoming_active_callback = NULL;
      return err;
    }
    return storage_result;
}

int signpost_storage_write (uint8_t* data, size_t len, Storage_Record_t* record_pointer) {
    storage_ready = false;
    storage_result = PORT_SUCCESS;
    callback_record = record_pointer;

    // set up callback
    if (incoming_active_callback != NULL) {
        return PORT_EBUSY;
    }
    incoming_active_callback = signpost_storage_write_callback;

    // allocate new message buffer
    size_t logname_len = strnlen(record_pointer->logname, STORAGE_LOG_LEN);
    uint8_t* marshal = (uint8_t*) malloc(logname_len + len + 1);
    memcpy(marshal, record_pointer->logname, logname_len+1);
    marshal[logname_len] = 0;
    memcpy(marshal+logname_len+1, data, len);
    // send message
    int err = signpost_api_send(StorageModuleAddress, CommandFrame,
            StorageApiType, StorageWriteMessage, len+logname_len+1, marshal);

    // free message buffer
    free(marshal);

    if (err < PORT_SUCCESS) {
        storage_ready = true;
        incoming_active_callback = NULL;
        return err;
    }

    // wait for response
    //err = port_signpost_wait_for_with_timeout(&storage_ready, 5000);
    //if (err != 0) {
    //  storage_ready = true;
    //  incoming_active_callback = NULL;
    //  return err;
    //}
    return storage_result;
}

int signpost_storage_read (uint8_t* data, size_t *len, Storage_Record_t * record_pointer) {
    storage_ready = false;
    storage_result = PORT_SUCCESS;
    callback_record = record_pointer;
    callback_data = data;
    callback_length = len;

    // set up callback
    if (incoming_active_callback != NULL) {
        return PORT_EBUSY;
    }
    incoming_active_callback = signpost_storage_read_callback;

    if(*len> record_pointer->length - record_pointer->offset) {
      *len= record_pointer->length - record_pointer->offset;
    }

    // allocate new message buffer
    size_t logname_len = strnlen(record_pointer->logname, STORAGE_LOG_LEN);
    size_t offset_len = sizeof(record_pointer->offset);
    size_t length_len = sizeof(*len);
    size_t marshal_len = logname_len + offset_len + length_len + 2;

    uint8_t* marshal = (uint8_t*) malloc(marshal_len);
    memset(marshal, 0, marshal_len);
    memcpy(marshal, &record_pointer->logname, logname_len);
    memcpy(marshal+logname_len+1, &record_pointer->offset, offset_len);
    memcpy(marshal+logname_len+1+offset_len+1, len, length_len);

    // send message
    int err = signpost_api_send(StorageModuleAddress, CommandFrame,
            StorageApiType, StorageReadMessage, marshal_len, marshal);

    // free message buffer
    free(marshal);

    if (err < PORT_SUCCESS) {
        storage_ready = true;
        incoming_active_callback = NULL;
        return err;
    }

    // wait for response
    err = port_signpost_wait_for_with_timeout(&storage_ready, 5000);
    if (err != 0) {
      storage_ready = true;
      incoming_active_callback = NULL;
      return err;
    }
    return storage_result;
}

int signpost_storage_delete (Storage_Record_t* record_pointer) {
    storage_ready = false;
    storage_result = PORT_SUCCESS;
    callback_record = record_pointer;

    // set up callback
    if (incoming_active_callback != NULL) {
        return PORT_EBUSY;
    }
    incoming_active_callback = signpost_storage_write_callback;

    // allocate new message buffer
    size_t logname_len = strnlen(record_pointer->logname, STORAGE_LOG_LEN);

    // send message
    int err = signpost_api_send(StorageModuleAddress, CommandFrame,
            StorageApiType, StorageDeleteMessage, logname_len, (uint8_t*) record_pointer->logname); if (err < PORT_SUCCESS) {
        storage_ready = true;
        incoming_active_callback = NULL;
        return err;
    }

    // wait for response
    //err = port_signpost_wait_for_with_timeout(&storage_ready, 5000);
    //if (err != 0) {
    //  storage_ready = true;
    //  incoming_active_callback = NULL;
    //  return err;
    //}
    return storage_result;
}

int signpost_storage_scan_reply(uint8_t destination_address, Storage_Record_t* list, size_t list_len) {
    return signpost_api_send(destination_address,
            ResponseFrame, StorageApiType, StorageScanMessage,
            list_len*sizeof(Storage_Record_t), (uint8_t*) list);
}

int signpost_storage_write_reply(uint8_t destination_address, Storage_Record_t* record_pointer) {
  return signpost_api_send(destination_address,
            ResponseFrame, StorageApiType, StorageWriteMessage,
            sizeof(Storage_Record_t), (uint8_t*) record_pointer);
}

int signpost_storage_read_reply(uint8_t destination_address, uint8_t* data, size_t length) {
    return signpost_api_send(destination_address,
            ResponseFrame, StorageApiType, StorageWriteMessage,
            length, data);
}

int signpost_storage_delete_reply(uint8_t destination_address, Storage_Record_t* record_pointer) {
    return signpost_api_send(destination_address,
            ResponseFrame, StorageApiType, StorageDeleteMessage,
            sizeof(Storage_Record_t), (uint8_t*) record_pointer);
}

/**************************************************************************/
/* PROCESSING API                                                         */
/**************************************************************************/
static bool processing_ready;
static void signpost_processing_callback(__attribute__((unused)) int result){
    processing_ready = true;
}

int signpost_processing_init(const char* path) {
    //form the sending message
    uint16_t size = strlen(path);
    uint16_t crc  = CRC16_Calc((uint8_t*)path,size,0xFFFF);
    uint8_t buf[size + 4];
    buf[0] = size & 0xff;
    buf[1] = ((size & 0xff00) > 8);
    buf[2] = crc & 0xff;
    buf[3] = ((crc & 0xff00) > 8);

    memcpy(buf+4,path,size);

    incoming_active_callback = signpost_processing_callback;
    processing_ready = false;

    int rc;
    rc = signpost_api_send(StorageModuleAddress,  CommandFrame,
             ProcessingApiType, ProcessingInitMessage, size+4, buf);
    if (rc < 0) return rc;

    //wait for a response
    port_signpost_wait_for(&processing_ready);

    if(incoming_message_length >= 5) {
        //this byte should be the return code
        return incoming_message[4];
    } else {
        //an erro
        return 1;
    }
}

int signpost_processing_oneway_send(uint8_t* buf, uint16_t len) {

    //form the sending message
    uint16_t crc  = CRC16_Calc(buf,len,0xFFFF);
    uint8_t b[len + 4];
    b[0] = len & 0xff;
    b[1] = ((len & 0xff00) > 8);
    b[2] = crc & 0xff;
    b[3] = ((crc & 0xff00) > 8);

    memcpy(b+4,buf,len);

    incoming_active_callback = signpost_processing_callback;

    int rc;
    rc = signpost_api_send(StorageModuleAddress,  CommandFrame,
             ProcessingApiType, ProcessingOneWayMessage, len+2, b);
    if (rc < 0) return rc;

    processing_ready = false;
    //wait for a response
    //the response is just an ack that it got there
    port_signpost_wait_for(&processing_ready);

    return incoming_message[0];
}

int signpost_processing_twoway_send(uint8_t* buf, uint16_t len) {
    //form the sending message
    uint16_t crc  = CRC16_Calc(buf,len,0xFFFF);
    uint8_t b[len + 4];
    b[0] = len & 0xff;
    b[1] = ((len & 0xff00) > 8);
    b[2] = crc & 0xff;
    b[3] = ((crc & 0xff00) > 8);

    memcpy(b+4,buf,len);

    incoming_active_callback = signpost_processing_callback;

    int rc;
    rc = signpost_api_send(StorageModuleAddress,  CommandFrame,
             ProcessingApiType, ProcessingTwoWayMessage, len+4, b);
    if (rc < 0) return rc;

    processing_ready = false;
    //wait for a response in the next function call
    //
    return ProcessingSuccess;
}

int signpost_processing_twoway_receive(uint8_t* buf, uint16_t* len) {

    port_signpost_wait_for(&processing_ready);

    //get the header and confirm it matches
    uint16_t size;
    uint16_t crc;
    memcpy(&size,incoming_message,2);
    memcpy(&crc,incoming_message+2,2);
    if(size != incoming_message_length - 4) {
        //an error occured
        return ProcessingSizeError;
    }

    if(crc != CRC16_Calc(incoming_message+4,size,0xFFFF)) {
        return ProcessingCRCError;
    }

    memcpy(buf,incoming_message+4,size);
    memcpy(len,&size,2);

    return ProcessingSuccess;
}

int signpost_processing_reply(uint8_t src_addr, uint8_t message_type, uint8_t* response,
                                    uint16_t response_len) {

   return signpost_api_send(src_addr, ResponseFrame, ProcessingApiType,
                        message_type, response_len, response);

}
/**************************************************************************/
/* NETWORKING API                                                         */
/**************************************************************************/


static bool networking_ready;
static bool networking_result;
static void signpost_networking_callback(int result) {
    networking_ready = true;
    networking_result = result;
}

int signpost_networking_send(const char* topic, uint8_t* data, uint8_t data_len) {
    uint8_t slen;
    if(strlen(topic) > 29) {
        slen = 29;
    } else {
        slen = strlen(topic);
    }

    uint32_t len = slen + data_len + 2;
    uint8_t* buf = malloc(len);
    if(!buf) {
        return PORT_ENOMEM;
    }

    buf[0] = slen;
    memcpy(buf+1, topic, slen);
    buf[slen+1] = data_len;
    memcpy(buf+1+slen+1, data, data_len);


    incoming_active_callback = signpost_networking_callback;
    networking_ready = false;
    int rc = signpost_api_send(RadioModuleAddress, CommandFrame, NetworkingApiType,
                        NetworkingSendMessage, len, buf);

    free(buf);
    if(rc < PORT_SUCCESS) {
        return rc;
    }

    rc = port_signpost_wait_for_with_timeout(&networking_ready, 3000);
    if(rc < PORT_SUCCESS) {
        return rc;
    }

    if(incoming_message_length >= 4) {
        return *(int*)incoming_message;
    } else {
        return PORT_FAIL;
    }
}

void signpost_networking_send_reply(uint8_t src_addr, uint8_t type, int return_code) {

   int rc = signpost_api_send(src_addr, ResponseFrame, NetworkingApiType,
                        type, 4, (uint8_t*)(&return_code));

   if (rc < 0) {
      port_printf(" - %d: Error sending networking reply (code: %d)\n", __LINE__, rc);
      signpost_api_error_reply_repeating(src_addr, NetworkingApiType,
            NetworkingSendMessage, rc, true, true, 1);
   }
}

/**************************************************************************/
/* ENERGY API                                                             */
/**************************************************************************/

static bool energy_query_ready;
static int  energy_query_result;
static signbus_app_callback_t* energy_cb = NULL;
static signpost_energy_information_t* energy_cb_data = NULL;
static bool energy_report_received;
static int energy_report_result;

static bool energy_reset_received;
static int energy_reset_result;

static void energy_query_sync_callback(int result) {
    SIGNBUS_DEBUG("result %d\n", result);
    energy_query_ready = true;
    energy_query_result = result;
}

static void signpost_energy_report_callback(int result) {
    energy_report_result = result;
    energy_report_received = true;
}

static void signpost_energy_reset_callback(int result) {
    energy_reset_result = result;
    energy_reset_received = true;
}

int signpost_energy_query(signpost_energy_information_t* energy) {
    energy_query_ready = false;

    {
        int rc = signpost_energy_query_async(energy, energy_query_sync_callback);
        if (rc != 0) {
            return rc;
        }
    }

    int ret = port_signpost_wait_for_with_timeout(&energy_query_ready, 10000);
    if(ret < 0) return PORT_FAIL;

    return energy_query_result;
}

static void energy_query_async_callback(int len_or_rc) {
    SIGNBUS_DEBUG("len_or_rc %d\n", len_or_rc);

    if (len_or_rc != sizeof(signpost_energy_information_t)) {
        port_printf("%s:%d - Error: bad len, got %d, want %d\n",
                __FILE__, __LINE__, len_or_rc, sizeof(signpost_energy_information_t));
    } else {
        if (energy_cb_data != NULL) {
            memcpy(energy_cb_data, incoming_message, len_or_rc);
        }
        energy_cb_data = NULL;
    }

    if (energy_cb != NULL) {
        // allow recursion
        signbus_app_callback_t* temp = energy_cb;
        energy_cb = NULL;
        temp(len_or_rc);
    }
}

int signpost_energy_query_async(
        signpost_energy_information_t* energy,
        signbus_app_callback_t cb
        ) {
    if (incoming_active_callback != NULL) {
        // XXX: Consider multiplexing based on API
        return -PORT_EBUSY;
    }
    if (energy_cb != NULL) {
        return -PORT_EBUSY;
    }
    incoming_active_callback = energy_query_async_callback;
    energy_cb_data = energy;
    energy_cb = cb;

    int rc;
    rc = signpost_api_send(ControlModuleAddress,
            CommandFrame, EnergyApiType, EnergyQueryMessage,
            0, NULL);

    // This properly catches the error if the send fails
    // and allows for subsequent calls to query async to succeed
    if (rc < 0) {
        //abort the transaction
        energy_cb_data = NULL;
        incoming_active_callback = NULL;
        energy_cb = NULL;
        return rc;
    };

    return PORT_SUCCESS;
}

int signpost_energy_duty_cycle(uint32_t time_ms) {
    return signpost_api_send(ControlModuleAddress,
            NotificationFrame, EnergyApiType, EnergyDutyCycleMessage,
            sizeof(uint32_t), (uint8_t*)&time_ms);
}

int signpost_energy_report(signpost_energy_report_t* report) {
    // Since we can take in a variable number of reports we
    // should make a message buffer and pack the reports into it.
    uint8_t reports_size = report->num_reports*sizeof(signpost_energy_report_module_t);
    uint8_t report_buf_size = reports_size + 1;
    uint8_t* report_buf = malloc(report_buf_size);
    if(!report_buf) {
        return PORT_ENOMEM;
    }

    report_buf[0] = report->num_reports;
    memcpy(report_buf+1,report->reports,reports_size);

    int rc;
    rc = signpost_api_send(ControlModuleAddress,
            CommandFrame, EnergyApiType, EnergyReportModuleConsumptionMessage,
            report_buf_size, report_buf);
    free(report_buf);
    if (rc < 0) return rc;

    incoming_active_callback = signpost_energy_report_callback;
    energy_report_received = false;
    rc = port_signpost_wait_for_with_timeout(&energy_report_received,10000);
    if(rc < 0) {
        return PORT_FAIL;
    }


    // There is an integer in the incoming message that should be
    // sent back as the return code.
    if(energy_report_result < 0) {
        return PORT_FAIL;
    } else {
        return *incoming_message;
    }
}

int signpost_energy_reset(void) {

    int rc;
    rc = signpost_api_send(ControlModuleAddress,
            CommandFrame, EnergyApiType, EnergyResetMessage,
            0, NULL);
    if (rc < 0) return rc;

    incoming_active_callback = signpost_energy_reset_callback;
    energy_reset_received = false;
    rc = port_signpost_wait_for_with_timeout(&energy_reset_received,10000);
    if(rc < 0) return PORT_FAIL;

    // There is an integer in the incoming message that should be
    // sent back as the return code.
    if(energy_reset_result < 0) {
        return PORT_FAIL;
    } else {
        return *incoming_message;
    }
}

int signpost_energy_query_reply(uint8_t destination_address,
        signpost_energy_information_t* info) {
    return signpost_api_send(destination_address,
            ResponseFrame, EnergyApiType, EnergyQueryMessage,
            sizeof(signpost_energy_information_t), (uint8_t*) info);
}

int signpost_energy_report_reply(uint8_t destination_address,
                                             int return_code) {

    return signpost_api_send(destination_address,
            ResponseFrame, EnergyApiType, EnergyReportModuleConsumptionMessage,
            sizeof(int), (uint8_t*)&return_code);
}

int signpost_energy_reset_reply(uint8_t destination_address, int return_code) {

    return signpost_api_send(destination_address,
            ResponseFrame, EnergyApiType, EnergyResetMessage,
            sizeof(int), (uint8_t*)&return_code);
}

/**************************************************************************/
/* TIME & LOCATION API                                                    */
/**************************************************************************/

static bool timelocation_query_answered;
static int  timelocation_query_result;

// Callback when a response is received
static void timelocation_callback(int result) {
    timelocation_query_answered = true;
    timelocation_query_result = result;
}

static int signpost_timelocation_sync(signpost_timelocation_message_type_e message_type) {
    // Variable we yield() on that is set to true when we get a response
    timelocation_query_answered = false;

    // Check that the internal buffers aren't already being used
    if (incoming_active_callback != NULL) {
        return PORT_EBUSY;
    }

    // Setup the callback that the API layer should use
    incoming_active_callback = timelocation_callback;

    // Call down to send the message
    int rc = signpost_api_send(ControlModuleAddress,
            CommandFrame, TimeLocationApiType, message_type,
            0, NULL);
    if (rc < 0) return rc;

    // Wait for a response message to come back
    port_signpost_wait_for(&timelocation_query_answered);

    // Check the response message type
    if (incoming_message_type != message_type) {
        // We got back a different response type?
        // This is bad, and unexpected.
        SIGNBUS_DEBUG("Wrong message type received. Expected: %d, got: %d\n",
            message_type, incoming_message_type);
        return PORT_FAIL;
    }

    return timelocation_query_result;
}

int signpost_timelocation_get_time(signpost_timelocation_time_t* time) {
    // Check the argument, because why not.
    if (time == NULL) {
        return PORT_EINVAL;
    }

    int rc = signpost_timelocation_sync(TimeLocationGetTimeMessage);
    if (rc < 0) return rc;

    // Do our due diligence
    if (incoming_message_length != sizeof(signpost_timelocation_time_t)) {
        SIGNBUS_DEBUG("Time message wrong length. Expected: %d, got %d\n",
            sizeof(signpost_timelocation_time_t), incoming_message_length);
        return PORT_FAIL;
    }

    memcpy(time, incoming_message, incoming_message_length);

    return rc;
}

int signpost_timelocation_get_location(signpost_timelocation_location_t* location) {
    // Check the argument, because why not.
    if (location == NULL) {
        return PORT_EINVAL;
    }

    int rc = signpost_timelocation_sync(TimeLocationGetLocationMessage);
    if (rc < 0) return rc;

    // Do our due diligence
    if (incoming_message_length != sizeof(signpost_timelocation_location_t)) {
        SIGNBUS_DEBUG("Location message wrong length. Expected: %d, got %d\n",
            sizeof(signpost_timelocation_location_t), incoming_message_length);
        return PORT_FAIL;
    }

    memcpy(location, incoming_message, incoming_message_length);

    return rc;
}

int signpost_timelocation_get_time_reply(uint8_t destination_address,
        signpost_timelocation_time_t* time) {
    return signpost_api_send(destination_address,
            ResponseFrame, TimeLocationApiType, TimeLocationGetTimeMessage,
            sizeof(signpost_timelocation_time_t), (uint8_t*) time);
}

int signpost_timelocation_get_location_reply(uint8_t destination_address,
        signpost_timelocation_location_t* location) {
    return signpost_api_send(destination_address,
            ResponseFrame, TimeLocationApiType, TimeLocationGetLocationMessage,
            sizeof(signpost_timelocation_location_t), (uint8_t*) location);
}

/**************************************************************************/
/* Watchdog API                                                           */
/**************************************************************************/
static bool watchdog_reply;

static void signpost_watchdog_cb(__attribute__ ((unused)) int result) {
    watchdog_reply = true;
}

int signpost_watchdog_start(void) {
    watchdog_reply = false;

    int rc = signpost_api_send(ControlModuleAddress, CommandFrame, WatchdogApiType,
            WatchdogStartMessage, 0, NULL);
    if(rc < 0) {
        return rc;
    }

    incoming_active_callback = signpost_watchdog_cb;

    port_signpost_wait_for(&watchdog_reply);

    return 1;
}

int signpost_watchdog_tickle(void) {
    watchdog_reply = false;

    int rc = signpost_api_send(ControlModuleAddress, CommandFrame, WatchdogApiType,
            WatchdogTickleMessage, 0, NULL);
    if(rc < 0) {
        return rc;
    }

    incoming_active_callback = signpost_watchdog_cb;

    port_signpost_wait_for(&watchdog_reply);

    return 1;
}

int signpost_watchdog_reply(uint8_t destination_address) {
    int rc = signpost_api_send(destination_address, ResponseFrame, WatchdogApiType,
            WatchdogResponseMessage,0, NULL);

    return rc;
}

/**************************************************************************/
/* EDISON API                                                             */
/**************************************************************************/

#pragma GCC diagnostic pop
