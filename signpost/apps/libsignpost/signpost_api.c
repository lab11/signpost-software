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

#define NAME_LEN 16
#define ECDH_KEY_LENGTH 32
#define NUM_MODULES 8
#define MOD_STATE_MAGIC 0xDEADBEEF

extern mbedtls_ctr_drbg_context ctr_drbg_context;

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


struct module_api_struct {
    api_handler_t**         api_handlers;
} module_api = {0};

static module_state_t module_info = {0};

// Translate module address to pairwise key
uint8_t* signpost_api_addr_to_key(uint8_t addr) {
    for (size_t i = 0; i < NUM_MODULES; i++) {
        if (addr == module_info.i2c_address_mods[i] && module_info.haskey[i]) {
            uint8_t* key = module_info.keys[i];
            port_printf("key: %p: 0x%02x%02x%02x...%02x\n", key,
                    key[0], key[1], key[2], key[ECDH_KEY_LENGTH-1]);
            return key;
        }
    }

    SIGNBUS_DEBUG("key: NULL\n");
    return NULL;
}

int signpost_api_addr_to_mod_num(uint8_t addr){
    for (size_t i = 0; i < NUM_MODULES; i++) {
        if (addr == module_info.i2c_address_mods[i]) {
            return i;
        }
    }
    port_printf("WARN: Do not have module registered to address 0x%x\n", addr);
    return PORT_FAIL;
}

uint8_t signpost_api_appid_to_mod_num(uint16_t appid) {
    return signpost_api_addr_to_mod_num(appid);
}

int signpost_api_revoke_key(uint8_t module_number) {
    if (module_number >= NUM_MODULES) return PORT_EINVAL;
    port_printf("WARN: Revoking key for module %d\n", module_number);
    module_info.haskey[module_number] = false;
    memset(module_info.keys[module_number], 0 , ECDH_KEY_LENGTH);
    return PORT_SUCCESS;
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

int signpost_api_send(uint8_t destination_address,
                      signbus_frame_type_t frame_type,
                      signbus_api_type_t api_type,
                      uint8_t message_type,
                      size_t message_length,
                      uint8_t* message) {

    int rc = signbus_app_send(destination_address, signpost_api_addr_to_key, frame_type, api_type,
                            message_type, message_length, message);

    //start recieving after sending!!
    signpost_api_start_new_async_recv();

    return rc;
}



static void signpost_api_recv_callback(int len_or_rc) {
    if (len_or_rc < 0) {
        if (len_or_rc == PORT_ECRYPT) {
            port_printf("Dropping message with HMAC/HASH failure\n");
            signpost_api_start_new_async_recv();
            return;
        } else {
            port_printf("%s:%d It's all fubar?\n", __FILE__, __LINE__);
            signpost_api_start_new_async_recv();
            return;
        }
    }
    if ( (incoming_frame_type == NotificationFrame) || (incoming_frame_type == CommandFrame) ) {
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
//static bool register_module_complete;
static bool request_isolation_complete;
//static bool revoke_complete;
static bool declare_controller_complete;
//static bool key_send_complete;

// state of isolation
static bool is_isolated = 0;

// module address currently initializing with
// first initialization is with controller
static uint8_t mod_addr_init = ModuleAddressController;

// mbedtls stuff
#define ECDH_BUF_LEN 72
static mbedtls_ecdh_context ecdh;
static size_t  ecdh_param_len;
static uint8_t ecdh_buf[ECDH_BUF_LEN];

/**************************************/
/* Initialization Callbacks           */
/**************************************/
static void signpost_initialization_declare_callback(int len_or_rc) {
    // Flip waiting variable, change state if well-formed message
    declare_controller_complete = true;
    if (len_or_rc < PORT_SUCCESS) return;
    if (incoming_api_type != InitializationApiType || incoming_message_type !=
            InitializationDeclare) return;

    init_state = Done;
}

static void signpost_initialization_isolation_callback(int unused __attribute__ ((unused))) {
    is_isolated = 1;
    if (init_state != RequestIsolation) return;
    // update state and flip waiting variable
    init_state = Declare;
    request_isolation_complete = true;
}

static void signpost_initialization_lost_isolation_callback(int unused __attribute__ ((unused))) {
    // reset state to request isolation if no longer isolated
    is_isolated = 0;
    if (init_state != Done)
      init_state = RequestIsolation;
}

/**************************************/
/* Initialization Helper Functions    */
/**************************************/

int signpost_initialization_request_isolation(void) {
    int rc;
    // Pull Mod_Out Low to signal controller
    // Wait on controller interrupt on MOD_IN
    rc = port_signpost_mod_out_clear();
    if (rc != PORT_SUCCESS) return rc;
    rc = port_signpost_debug_led_on();
    if (rc != PORT_SUCCESS) return rc;

    return PORT_SUCCESS;
}

static int signpost_initialization_declare_controller(void) {
    // set callback for handling response from controller/modules
    if (incoming_active_callback != NULL && incoming_active_callback != signpost_initialization_declare_callback) {
        return PORT_EBUSY;
    }

    incoming_active_callback = signpost_initialization_declare_callback;

    if (signpost_api_send(ModuleAddressController, CommandFrame,
          InitializationApiType, InitializationDeclare, strnlen(module_info.self_name,NAME_LEN),
          (uint8_t*)module_info.self_name) >= PORT_SUCCESS) {
        return PORT_SUCCESS;
    }

    return PORT_FAIL;
}

int signpost_initialization_declare_respond(uint8_t source_address, uint8_t new_address, uint8_t module_number, char* name) {

    if (module_info.i2c_address_mods[module_number] != new_address) {
      module_info.i2c_address_mods[module_number] = new_address;
      strncpy(module_info.names[module_number],name,strnlen(name,NAME_LEN));
      module_info.haskey[module_number] = false;
    }

    port_printf("INIT: Registered address 0x%x at slot %d with name %s\n", new_address, module_number, name);

    //send i2c address
    return signpost_api_send(source_address, ResponseFrame, InitializationApiType, InitializationDeclare, 1, &new_address);
}

int signpost_initialization_key_exchange_respond(uint8_t source_address, uint8_t* ecdh_params, size_t len) {
    int ret = PORT_SUCCESS;
    uint8_t module_number = signpost_api_addr_to_mod_num(source_address);

    port_printf("INIT: Performing key exchange with module %d\n", module_number);

    // init ecdh struct for key exchange
    mbedtls_ecdh_free(&ecdh);
    mbedtls_ecdh_init(&ecdh);
    ret = mbedtls_ecp_group_load(&ecdh.grp, MBEDTLS_ECP_DP_SECP256R1);
    if(ret < PORT_SUCCESS) return ret;

    // read params from contacting module
    ret = mbedtls_ecdh_read_params(&ecdh, (const uint8_t **) &ecdh_params, ecdh_params+len);
    if(ret < PORT_SUCCESS) return ret;

    // make params
    ret = mbedtls_ecdh_make_public(&ecdh, &ecdh_param_len, ecdh_buf, ECDH_BUF_LEN, mbedtls_ctr_drbg_random, &ctr_drbg_context);
    if(ret < PORT_SUCCESS) return ret;

    if (module_number == 0xff) return PORT_FAIL;
    uint8_t* key = module_info.keys[module_number];
    size_t keylen;
    // calculate shared secret
    ret = mbedtls_ecdh_calc_secret(&ecdh, &keylen, key, ECDH_KEY_LENGTH, mbedtls_ctr_drbg_random, &ctr_drbg_context);
    if(ret < PORT_SUCCESS) return ret;
    SIGNBUS_DEBUG("key: %p: 0x%02x%02x%02x...%02x\n", key,
            key[0], key[1], key[2], key[ECDH_KEY_LENGTH-1]);
    ret = signpost_api_send(source_address,
            ResponseFrame, InitializationApiType, InitializationKeyExchange,
            ecdh_param_len, ecdh_buf);

    module_info.haskey[module_number] = true;

    //port_signpost_save_state(&module_info);
    return ret;
}

static int signpost_initialization_common(uint8_t i2c_address) {
    SIGNBUS_DEBUG("i2c %02x handlers %p\n", i2c_address, api_handlers);

    int rc;

    // Initialize the lower layers
    signbus_io_init(i2c_address);
    rc = signpost_entropy_init();
    if (rc < 0) return rc;
    // See comment in protocol_layer.h
    signbus_protocol_setup_async(incoming_protocol_buffer, INCOMING_MESSAGE_BUFFER_LENGTH);
    // Clear keys
    for (int i=0; i < NUM_MODULES; i++) {
        module_info.haskey[i] = false;
        memset(module_info.keys[i], 0, ECDH_KEY_LENGTH);
        module_info.i2c_address_mods[i] = 0xff;
    }

    module_info.i2c_address_mods[3] = ModuleAddressController;
    module_info.i2c_address_mods[4] = ModuleAddressStorage;

    module_info.magic = MOD_STATE_MAGIC;

    return PORT_SUCCESS;
}

int signpost_initialization_controller_module_init(api_handler_t** api_handlers) {
    //module_state_t check_state;
    int rc = signpost_initialization_common(ModuleAddressController);
    if (rc < 0) return rc;

    // Save module configuration
    module_info.i2c_address = ModuleAddressController;
    module_api.api_handlers = api_handlers;

    //add the controller name
    strncpy(module_info.self_name,"lab11/control",NAME_LEN);

    // Begin listening for replies
    signpost_api_start_new_async_recv();

    SIGNBUS_DEBUG("complete\n");
    return PORT_SUCCESS;
}

static int signpost_initialization_initialize_loop(void) {
    int rc;
    //module_state_t check_state;
    //bool keys_exist;

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

            break;
          case Declare:
            {

            // check that mod_in is still low after delay
            port_signpost_delay_ms(50);
            if(!is_isolated && port_signpost_mod_in_read() != 0) {
              init_state = RequestIsolation;
              break;
            }
            port_printf("INIT: Granted I2C isolation and started initialization with Controller\n");

            // Now declare self to controller
            declare_controller_complete = false;
            port_signpost_delay_ms(500);
            rc = signpost_initialization_declare_controller();
            if (rc != PORT_SUCCESS) {
                port_printf("INIT: Declaration Failed - Requesting Isolation\n");
                port_signpost_mod_out_set();
                port_signpost_delay_ms(3000);
                incoming_active_callback = NULL;
                init_state = RequestIsolation;
                break;
            }

            rc = port_signpost_wait_for_with_timeout(&declare_controller_complete, 100);
            if (rc != PORT_SUCCESS) {
              port_printf("INIT: Timed out waiting for controller declare response\n");
              port_signpost_mod_out_set();
              port_signpost_delay_ms(3000);
              incoming_active_callback = NULL;
              init_state = RequestIsolation;
              break;
            };

            //reinitialize with the new address
            uint8_t new_address = *incoming_message;
            if(module_info.i2c_address != new_address){
                module_info.i2c_address = new_address;
                signpost_initialization_common(new_address);
            }

            port_printf("INIT: Set I2C address to 0x%x\n", new_address);

            }
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

int signpost_init(const char* org_name, const char* module_name) {
    int rc;

    //setup APIS
    static api_handler_t* api_handlers[] = {NULL};
    rc = signpost_initialization_common(0x00);
    if (rc < PORT_SUCCESS) return rc;

    // Save module configuration
    module_info.i2c_address = 0x00;
    module_api.api_handlers = api_handlers;


    //copy the name into the info struct
    uint8_t org_name_len = strnlen(org_name,NAME_LEN);
    if(org_name_len <= 0) {
        return PORT_EINVAL;
    }

    uint8_t module_name_len = strnlen(module_name,NAME_LEN);
    if(module_name_len <= 0) {
        return PORT_EINVAL;
    }

    strncpy(module_info.self_name,org_name,org_name_len);
    module_info.self_name[org_name_len] = '/';
    strncpy(module_info.self_name + org_name_len + 1,module_name,module_name_len);

    // Begin listening for replies
    signpost_api_start_new_async_recv();

    // Initialize Mod Out/In GPIO
    // both are active low
    port_signpost_mod_out_set();
    port_signpost_debug_led_off();

    // setup interrupts for changes in isolated state
    rc = port_signpost_mod_in_enable_interrupt_rising(signpost_initialization_lost_isolation_callback);
    if (rc != PORT_SUCCESS) return rc;

    rc = port_signpost_mod_in_enable_interrupt_falling(signpost_initialization_isolation_callback);
    if (rc != PORT_SUCCESS) return rc;

    init_state = RequestIsolation;
    return signpost_initialization_initialize_loop();
}

int signpost_initialization_module_init(const char* module_name, uint8_t i2c_address, api_handler_t** api_handlers) {
    int rc;

    rc = signpost_initialization_common(i2c_address);
    if (rc < PORT_SUCCESS) return rc;

    // Save module configuration
    module_info.i2c_address = i2c_address;
    module_api.api_handlers = api_handlers;

    //copy the name into the info struct
    strncpy(module_info.self_name,module_name,NAME_LEN);

    // Begin listening for replies
    signpost_api_start_new_async_recv();

    // Initialize Mod Out/In GPIO
    // both are active low
    port_signpost_mod_out_set();
    port_signpost_debug_led_off();

    // setup interrupts for changes in isolated state
    rc = port_signpost_mod_in_enable_interrupt_rising(signpost_initialization_lost_isolation_callback);
    if (rc != PORT_SUCCESS) return rc;

    rc = port_signpost_mod_in_enable_interrupt_falling(signpost_initialization_isolation_callback);
    if (rc != PORT_SUCCESS) return rc;

    init_state = RequestIsolation;
    return signpost_initialization_initialize_loop();
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
    int err = signpost_api_send(ModuleAddressStorage, CommandFrame,
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
    int err = signpost_api_send(ModuleAddressStorage, CommandFrame,
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
    int err = signpost_api_send(ModuleAddressStorage, CommandFrame,
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
    int err = signpost_api_send(ModuleAddressStorage, CommandFrame,
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
    rc = signpost_api_send(ModuleAddressStorage,  CommandFrame,
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
    rc = signpost_api_send(ModuleAddressStorage,  CommandFrame,
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
    rc = signpost_api_send(ModuleAddressStorage,  CommandFrame,
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
static signpost_networking_subscribe_cb_t networking_subscribe_cb = NULL;

static void internal_subscribe_callback(__attribute__ ((unused)) uint8_t source_address,
        signbus_frame_type_t frame_type, __attribute ((unused)) signbus_api_type_t api_type,
        uint8_t message_type, size_t message_length, uint8_t* message) {

    //If there is no callback just drop the message
    if(networking_subscribe_cb == NULL) {
        return;
    } else {
        if(api_type == NetworkingApiType &&
                frame_type == NotificationFrame &&
                message_type == NetworkingSubscribeMessage) {
            //extract the topic and the data
            //make the topic string static for easier processing
            static char topic[29];
            uint8_t tlen = message[0];
            if(tlen > 28 || message_length < tlen + (unsigned)2) {
                //invalid topic
                return;
            }
            memcpy(topic, message+1, tlen);

            uint8_t dlen = message[tlen+1];
            if(message_length < tlen + dlen + (unsigned)2) {
                return;
            }
            uint8_t* data = message+tlen+2;

            //call the provided callback
            networking_subscribe_cb(topic, data, dlen);
        } else {
            return;
        }
    }
}

static void signpost_networking_callback(int result) {
    networking_ready = true;
    networking_result = result;
}

int signpost_networking_publish(const char* topic, uint8_t* data, uint8_t data_len) {
    uint8_t slen;
    if(strnlen(topic, 14) > 14) {
        slen = 14;
    } else {
        slen = strnlen(topic, 14);
    }

    uint8_t nlen = strnlen(module_info.self_name, NAME_LEN);
    uint8_t slash = 0;
    if(topic[0] != '/' && module_info.self_name[nlen-1] != '/') {
        slash = 1;
    }

    uint32_t len = nlen + slash + slen + data_len + 2;
    uint8_t* buf = malloc(len);
    if(!buf) {
        return PORT_ENOMEM;
    }

    buf[0] = slen + nlen + slash;
    memcpy(buf+1, topic, slen);
    if(slash) {
        buf[slen+1] = '/';
    }
    memcpy(buf+1+slen+slash, module_info.self_name, nlen);
    buf[slen+nlen+slash+1] = data_len;
    memcpy(buf+1+slen+1+nlen+slash, data, data_len);


    incoming_active_callback = signpost_networking_callback;
    networking_ready = false;
    int rc = signpost_api_send(ModuleAddressRadio, CommandFrame, NetworkingApiType,
                        NetworkingPublishMessage, len, buf);

    free(buf);
    if(rc < PORT_SUCCESS) {
        incoming_active_callback = NULL;
        return rc;
    }

    rc = port_signpost_wait_for_with_timeout(&networking_ready, 3000);
    if(rc < PORT_SUCCESS) {
        incoming_active_callback = NULL;
        return rc;
    }

    if(incoming_message_length >= 4) {
        return *(int*)incoming_message;
    } else {
        return PORT_FAIL;
    }
}

int signpost_networking_subscribe(signpost_networking_subscribe_cb_t cb) {
    //store the callback in our callback field
    networking_subscribe_cb = cb;

    //register our internal callback with the app recv handler
    // get number of api_handlers and insert initialization handler
    uint8_t i = 0;
    while(module_api.api_handlers + (i++) != NULL) {
        //You can't do this if a networking handler is already registered
        if(module_api.api_handlers[i]->api_type == NetworkingApiType){
            return PORT_EINVAL;
        }
    }

    //Initialize a new handlers array
    api_handler_t** updated_api_handlers = (api_handler_t**) malloc(i*sizeof(api_handler_t*));
    if(updated_api_handlers == NULL) {
        return PORT_ENOMEM;
    }

    static api_handler_t subscribe_handler  = {NetworkingApiType, internal_subscribe_callback};
    updated_api_handlers[0] = &subscribe_handler;
    memcpy(updated_api_handlers+1, module_api.api_handlers, i-1);
    return PORT_SUCCESS;
}

int signpost_networking_publish_reply(uint8_t src_addr, int return_code) {

    return signpost_api_send(src_addr, ResponseFrame, NetworkingApiType,
                         NetworkingPublishMessage, 4, (uint8_t*)(&return_code));

}

int signpost_networking_subscribe_send(uint8_t dest_addr, char* topic, uint8_t* data, uint8_t data_len) {

    uint8_t tlen = strnlen(topic, 28);
    uint8_t* buf = malloc(tlen + data_len + 2);
    if(!buf) {
        return PORT_ENOMEM;
    }

    buf[0] = tlen;
    memcpy(buf + 1, topic, tlen);
    buf[1 + tlen] = data_len;
    memcpy(buf + 2 + tlen, data, data_len);

    int rc = signpost_api_send(dest_addr, NotificationFrame, NetworkingApiType,
                         NetworkingSubscribeMessage, 2+tlen+data_len, buf);

    free(buf);

    return rc;
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
    if(ret < 0) {
        incoming_active_callback = NULL;
        return PORT_FAIL;
    }

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
        return PORT_EBUSY;
    }
    if (energy_cb != NULL) {
        return PORT_EBUSY;
    }
    incoming_active_callback = energy_query_async_callback;
    energy_cb_data = energy;
    energy_cb = cb;

    int rc;
    rc = signpost_api_send(ModuleAddressController,
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
    return signpost_api_send(ModuleAddressController,
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
    rc = signpost_api_send(ModuleAddressController,
            CommandFrame, EnergyApiType, EnergyReportModuleConsumptionMessage,
            report_buf_size, report_buf);
    free(report_buf);
    if (rc < 0) return rc;

    incoming_active_callback = signpost_energy_report_callback;
    energy_report_received = false;
    rc = port_signpost_wait_for_with_timeout(&energy_report_received,10000);
    if(rc < 0) {
        incoming_active_callback = NULL;
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
    rc = signpost_api_send(ModuleAddressController,
            CommandFrame, EnergyApiType, EnergyResetMessage,
            0, NULL);
    if (rc < 0) return rc;

    incoming_active_callback = signpost_energy_reset_callback;
    energy_reset_received = false;
    rc = port_signpost_wait_for_with_timeout(&energy_reset_received,10000);
    if(rc < 0) {
        incoming_active_callback = NULL;
        return PORT_FAIL;
    }

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
    int rc = signpost_api_send(ModuleAddressController,
            CommandFrame, TimeLocationApiType, message_type,
            0, NULL);

    if (rc < 0)  {
        incoming_active_callback = NULL;
        return rc;
    }

    // Wait for a response message to come back
    rc = port_signpost_wait_for_with_timeout(&timelocation_query_answered, 1000);
    if (rc < 0)  {
        incoming_active_callback = NULL;
        return rc;
    }

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

int signpost_timelocation_get_time(time_t* time) {
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

    //put the incoming message into a temporary struct
    signpost_timelocation_time_t temp;
    memcpy(&temp, incoming_message, incoming_message_length);

    //convert that struct into a tm struct
    struct tm current_time;
    current_time.tm_year = temp.year - 1900;
    current_time.tm_mon = temp.month - 1;
    current_time.tm_mday = temp.day;
    current_time.tm_hour = temp.hours;
    current_time.tm_min = temp.minutes;
    current_time.tm_sec = temp.seconds;
    current_time.tm_isdst = 0;

    //convert it into a time_t object
    time_t utime = mktime(&current_time);

    //place it back in the starting array
    memcpy(time,&utime,sizeof(time_t));

    if(temp.satellite_count < 3) {
        return PORT_ENOSAT;
    } else {
        return temp.satellite_count;
    }
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

    int rc = signpost_api_send(ModuleAddressController, CommandFrame, WatchdogApiType,
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

    int rc = signpost_api_send(ModuleAddressController, CommandFrame, WatchdogApiType,
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
