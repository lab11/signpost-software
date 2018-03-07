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

#include "mbedtls/ecdh.h"
#include "mbedtls/ecp.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstack-usage="
#if CC_VERSION_MAJOR >= 7
#pragma GCC diagnostic ignored "-Wformat-truncation="
#endif

#define NUM_MODULES 8


static struct module_struct {
    uint8_t                 i2c_address;
    api_handler_t**         api_handlers;
    int8_t                  api_type_to_module_address[HighestApiType+1];
    uint8_t                 i2c_address_mods[NUM_MODULES];
    bool                    haskey[NUM_MODULES];
    uint8_t                 keys[NUM_MODULES][ECDH_KEY_LENGTH];
} module_info = {.i2c_address_mods = {ModuleAddressController, ModuleAddressStorage, ModuleAddressRadio}};


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
    return SB_PORT_FAIL;
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
    SIGNBUS_DEBUG("len_or_rc %d\n", len_or_rc);
    if (len_or_rc < 0) {
        if (len_or_rc == -94) {
            // These return codes are a hack
            port_printf("Dropping message with HMAC/HASH failure\n");
            signpost_api_start_new_async_recv();
        } else {
            port_printf("%s:%d It's all fubar?\n", __FILE__, __LINE__);
            // XXX trip watchdog reset or s/t?
            return;
        }
    }
    if ( (incoming_frame_type == NotificationFrame) || (incoming_frame_type == CommandFrame) ) {
        api_handler_t** handler = module_info.api_handlers;
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
static bool request_isolation_complete;
static bool declare_controller_complete;
static bool key_send_complete;

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
    if (len_or_rc < SB_PORT_SUCCESS) return;
    if (incoming_api_type != InitializationApiType || incoming_message_type !=
            InitializationDeclare) return;

    init_state = KeyExchange;
}
static void signpost_initialization_key_exchange_callback(int len_or_rc) {
    key_send_complete= true;
    if (len_or_rc < SB_PORT_SUCCESS) return;
    if (incoming_api_type != InitializationApiType || incoming_message_type !=
            InitializationKeyExchange) return;

    init_state = FinishExchange;
}

static void signpost_initialization_isolation_callback(int unused __attribute__ ((unused))) {
    if (init_state != RequestIsolation) return;
    // update state and flip waiting variable
    init_state = Isolated;
    request_isolation_complete = true;
}

static void signpost_initialization_lost_isolation_callback(int unused __attribute__ ((unused))) {
    // reset state to request isolation if no longer isolated
    init_state = RequestIsolation;
}

/**************************************/
/* Initialization Helper Functions    */
/**************************************/

int signpost_initialization_request_isolation(void) {
    int rc;
    rc = port_signpost_mod_in_enable_interrupt_falling(signpost_initialization_isolation_callback);
    if (rc != SB_PORT_SUCCESS) return rc;

    // Pull Mod_Out Low to signal controller
    // Wait on controller interrupt on MOD_IN
    rc = port_signpost_mod_out_clear();
    if (rc != SB_PORT_SUCCESS) return rc;
    rc = port_signpost_debug_led_on();
    if (rc != SB_PORT_SUCCESS) return rc;

    port_printf("INIT: Requested I2C isolation with controller\n");
    return SB_PORT_SUCCESS;
}

static int signpost_initialization_declare_controller(void) {
    // set callback for handling response from controller/modules
    if (incoming_active_callback != NULL) {
        return SB_PORT_EBUSY;
    }

    incoming_active_callback = signpost_initialization_declare_callback;

    // XXX also report APIs supported
    // XXX dynamic i2c address allocation
    if (signpost_api_send(ModuleAddressController, CommandFrame,
          InitializationApiType, InitializationDeclare, 1,
          &module_info.i2c_address) >= SB_PORT_SUCCESS) {
        return SB_PORT_SUCCESS;
    }

    return SB_PORT_FAIL;
}

static int signpost_initialization_key_exchange_finish(void) {
    // read params from contacted module
    if (mbedtls_ecdh_read_public(&ecdh, incoming_message,
                incoming_message_length) < 0) {
        //port_printf("failed to read public parameters\n");
        return SB_PORT_FAIL;
    }

    uint8_t  module_number = signpost_api_addr_to_mod_num(incoming_source_address);
    if (module_number == 0xff) return SB_PORT_FAIL;
    uint8_t* key = module_info.keys[module_number];
    size_t keylen;
    // generate key
    if(mbedtls_ecdh_calc_secret(&ecdh, &keylen, key, ECDH_KEY_LENGTH,
                mbedtls_ctr_drbg_random, &ctr_drbg_context) < 0) {
        //port_printf("failed to calculate secret\n");
        return SB_PORT_FAIL;
    }
    module_info.haskey[signpost_api_addr_to_mod_num(incoming_source_address)] =
        true;

    SIGNBUS_DEBUG("key: %p: 0x%02x%02x%02x...%02x\n", key,
            key[0], key[1], key[2], key[ECDH_KEY_LENGTH-1]);

    port_printf("INIT: Initialization with module %d complete\n", signpost_api_addr_to_mod_num(incoming_source_address));
    return 0;
}

int signpost_initialization_key_exchange_send(uint8_t destination_address) {
    int rc;
    port_printf("INIT: Granted I2C isolation and started initialization with module %d\n", signpost_api_addr_to_mod_num(destination_address));
    // set callback for handling response from controller/modules
    if (incoming_active_callback != NULL) {
        return SB_PORT_EBUSY;
    }
    incoming_active_callback = signpost_initialization_key_exchange_callback;


    // Prepare for ECDH key exchange
    mbedtls_ecdh_init(&ecdh);
    rc = mbedtls_ecp_group_load(&ecdh.grp,MBEDTLS_ECP_DP_SECP256R1);
    if (rc < 0) return rc;
    rc = mbedtls_ecdh_make_params(&ecdh, &ecdh_param_len, ecdh_buf,
            ECDH_BUF_LEN, mbedtls_ctr_drbg_random, &ctr_drbg_context);
    if (rc < 0) return rc;

    // Now have a private channel with the controller
    // Key exchange with module, send ecdh params
    if (signpost_api_send(destination_address, CommandFrame, InitializationApiType,
            InitializationKeyExchange, ecdh_param_len, ecdh_buf) > 0) {
      return SB_PORT_SUCCESS;
    }
    else return SB_PORT_FAIL;
}

int signpost_initialization_declare_respond(uint8_t source_address, uint8_t module_number) {
    //int ret = SB_PORT_SUCCESS;

    //XXX choose address to respond to module

    module_info.i2c_address_mods[module_number] = source_address;
    module_info.haskey[module_number] = false;

    port_printf("INIT: Registered address 0x%x as module %d\n", source_address, module_number);
    // Just ack, eventually will send new address
    return signpost_api_send(source_address, ResponseFrame, InitializationApiType, InitializationDeclare, 1, &module_number);
}
int signpost_initialization_key_exchange_respond(uint8_t source_address, uint8_t* ecdh_params, size_t len) {
    int ret = SB_PORT_SUCCESS;

    port_printf("INIT: Performing key exchange with module %d\n", signpost_api_addr_to_mod_num(source_address));
    // init ecdh struct for key exchange
    mbedtls_ecdh_free(&ecdh);
    mbedtls_ecdh_init(&ecdh);
    ret = mbedtls_ecp_group_load(&ecdh.grp, MBEDTLS_ECP_DP_SECP256R1);
    if(ret < SB_PORT_SUCCESS) return ret;

    // read params from contacting module
    ret = mbedtls_ecdh_read_params(&ecdh, (const uint8_t **) &ecdh_params, ecdh_params+len);
    if(ret < SB_PORT_SUCCESS) return ret;

    // make params
    ret = mbedtls_ecdh_make_public(&ecdh, &ecdh_param_len, ecdh_buf, ECDH_BUF_LEN, mbedtls_ctr_drbg_random, &ctr_drbg_context);
    if(ret < SB_PORT_SUCCESS) return ret;

    // get key address of contacting module
    uint8_t  module_number = signpost_api_addr_to_mod_num(incoming_source_address);
    if (module_number == 0xff) return SB_PORT_FAIL;
    uint8_t* key = module_info.keys[module_number];
    size_t keylen;
    // calculated shared secret
    ret = mbedtls_ecdh_calc_secret(&ecdh, &keylen, key, ECDH_KEY_LENGTH, mbedtls_ctr_drbg_random, &ctr_drbg_context);
    if(ret < SB_PORT_SUCCESS) return ret;
    SIGNBUS_DEBUG("key: %p: 0x%02x%02x%02x...%02x\n", key,
            key[0], key[1], key[2], key[ECDH_KEY_LENGTH-1]);
    ret = signpost_api_send(source_address,
            ResponseFrame, InitializationApiType, InitializationKeyExchange,
            ecdh_param_len, ecdh_buf);

    module_info.haskey[signpost_api_addr_to_mod_num(source_address)] = true;

    return ret;
}

static int signpost_initialization_common(uint8_t i2c_address, api_handler_t** api_handlers) {
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

    // Save module configuration
    module_info.i2c_address = i2c_address;
    module_info.api_handlers = api_handlers;

    // Populate the well-known API types with fixed addresses
    module_info.api_type_to_module_address[InitializationApiType] = ModuleAddressController;
    module_info.api_type_to_module_address[WatchdogApiType] = ModuleAddressController;
    module_info.api_type_to_module_address[StorageApiType] = ModuleAddressStorage;
    module_info.api_type_to_module_address[NetworkingApiType] = -1; /* not supported */
    module_info.api_type_to_module_address[ProcessingApiType] = -1; /* not supported */
    module_info.api_type_to_module_address[EnergyApiType] = ModuleAddressController;
    module_info.api_type_to_module_address[TimeLocationApiType] = ModuleAddressController;

    module_info.i2c_address_mods[3] = ModuleAddressController;
    module_info.i2c_address_mods[4] = ModuleAddressStorage;

    return SB_PORT_SUCCESS;
}

int signpost_initialization_controller_module_init(api_handler_t** api_handlers) {
    int rc = signpost_initialization_common(ModuleAddressController, api_handlers);
    if (rc < 0) return rc;

    // Begin listening for replies
    signpost_api_start_new_async_recv();

    SIGNBUS_DEBUG("complete\n");
    return SB_PORT_SUCCESS;
}

int signpost_initialization_module_init(uint8_t i2c_address, api_handler_t** api_handlers) {
    int rc;
    rc = signpost_initialization_common(i2c_address, api_handlers);
    if (rc < SB_PORT_SUCCESS) return rc;

    // Begin listening for replies
    signpost_api_start_new_async_recv();

    // Initialize Mod Out/In GPIO
    // both are active low
    port_signpost_mod_out_set();
    port_signpost_debug_led_off();

    while(1) {
        switch(init_state) {
          case RequestIsolation:
            request_isolation_complete = false;
            rc = signpost_initialization_request_isolation();
            if (rc != SB_PORT_SUCCESS) {
              break;
            }

            rc = port_signpost_wait_for_with_timeout(&request_isolation_complete, 5000);
            if (rc == SB_PORT_FAIL) {
              port_printf("INIT: Timed out waiting for controller isolation\n");
              init_state = RequestIsolation;
            };

            break;
          case Isolated:
            // check that mod_in is still low after delay
            port_signpost_delay_ms(50);
            if(port_signpost_mod_in_read() != 0) {
              init_state = RequestIsolation;
              break;
            }
            // Now isolated with controller
            // setup interrupt for change in isolated state
            rc = port_signpost_mod_in_enable_interrupt_rising(signpost_initialization_lost_isolation_callback);
            if (rc != SB_PORT_SUCCESS) return rc;

            // Now declare self to controller
            declare_controller_complete = false;
            rc = signpost_initialization_declare_controller();
            if (rc != SB_PORT_SUCCESS) {
              break;
            }

            rc = port_signpost_wait_for_with_timeout(&declare_controller_complete, 100);
            if (rc == SB_PORT_FAIL) {
              port_printf("INIT: Timed out waiting for controller declare response\n");
              init_state = RequestIsolation;
            };

            break;
          case KeyExchange:
            // Send key exchange request
            key_send_complete = false;
            rc = signpost_initialization_key_exchange_send(incoming_source_address);
            if (rc != SB_PORT_SUCCESS) {
              // if key exchange send failed for some reason, restart from the
              // beginning
              init_state = RequestIsolation;
            }

            rc = port_signpost_wait_for_with_timeout(&key_send_complete, 5000);
            if (rc == SB_PORT_FAIL) {
              port_printf("INIT: Timed out waiting for controller key exchange response\n");
              init_state = RequestIsolation;
            };
            break;
          case FinishExchange:
            // Disable any existing interrupts, we have our key
            port_signpost_mod_in_disable_interrupt();
            rc = signpost_initialization_key_exchange_finish();
            if (rc == SB_PORT_SUCCESS) {
              init_state = Done;
            } else {
              // if key exchange failed for some reason, restart from the
              // beginning
              init_state = RequestIsolation;
            }

            break;
          case Done:
            port_signpost_mod_in_disable_interrupt();
            port_signpost_mod_out_set();
            port_signpost_debug_led_off();

            SIGNBUS_DEBUG("INIT: complete\n");
            return SB_PORT_SUCCESS;

          default:
            break;
        }
    }
    return 0;
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
    if (len_or_rc < SB_PORT_SUCCESS) {
        // error code response
        storage_result = len_or_rc;
    } else if ((size_t) len_or_rc > *callback_length * sizeof(Storage_Record_t)) {
        // invalid response length
        port_printf("%s:%d - Error: bad len, got %d, want %d\n",
                __FILE__, __LINE__, len_or_rc, *callback_length*sizeof(Storage_Record_t));
        storage_result = SB_PORT_FAIL;
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
        storage_result = SB_PORT_SUCCESS;
    }

    // response received
    storage_ready = true;
}

static void signpost_storage_write_callback(int len_or_rc) {
    if (len_or_rc < SB_PORT_SUCCESS) {
        // error code response
        storage_result = len_or_rc;
    } else if (len_or_rc != sizeof(Storage_Record_t)) {
        // invalid response length
        port_printf("%s:%d - Error: bad len, got %d, want %d\n",
                __FILE__, __LINE__, len_or_rc, sizeof(Storage_Record_t));
        storage_result = SB_PORT_FAIL;
    } else {
        // valid storage record
        if (callback_record != NULL) {
            // copy over record response
            memcpy(callback_record, incoming_message, len_or_rc);
        }
        callback_record = NULL;
        storage_result = SB_PORT_SUCCESS;
    }

    // response received
    storage_ready = true;
}

static void signpost_storage_read_callback(int len_or_rc) {
    if (len_or_rc < SB_PORT_SUCCESS) {
        // error code response
        storage_result = len_or_rc;
    } else if (callback_length == NULL) {
        storage_result = SB_PORT_EINVAL;
    } else if ((size_t) len_or_rc > *callback_length) {
        // invalid response length
        port_printf("%s:%d - Error: bad len, got %d, want %d\n",
                __FILE__, __LINE__, len_or_rc, *callback_length);
        storage_result = SB_PORT_FAIL;
    } else {
        // valid data
        if (callback_data != NULL) {
            // copy over record response
            memcpy(callback_data, incoming_message, *callback_length);
        }
        callback_data= NULL;
        callback_length = NULL;
        storage_result = SB_PORT_SUCCESS;
    }

    // response received
    storage_ready = true;
}

int signpost_storage_scan (Storage_Record_t* record_list, size_t* list_len) {
    storage_ready = false;
    storage_result = SB_PORT_SUCCESS;
    callback_record = record_list;
    callback_length = list_len;

    // set up callback
    if (incoming_active_callback != NULL) {
        return SB_PORT_EBUSY;
    }
    incoming_active_callback = signpost_storage_scan_callback;

    // send message
    int err = signpost_api_send(ModuleAddressStorage, CommandFrame,
            StorageApiType, StorageScanMessage, sizeof(*list_len), (uint8_t*) list_len);

    if (err < SB_PORT_SUCCESS) {
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
    storage_result = SB_PORT_SUCCESS;
    callback_record = record_pointer;

    // set up callback
    if (incoming_active_callback != NULL) {
        return SB_PORT_EBUSY;
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

    if (err < SB_PORT_SUCCESS) {
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
    storage_result = SB_PORT_SUCCESS;
    callback_record = record_pointer;
    callback_data = data;
    callback_length = len;

    // set up callback
    if (incoming_active_callback != NULL) {
        return SB_PORT_EBUSY;
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

    if (err < SB_PORT_SUCCESS) {
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
    storage_result = SB_PORT_SUCCESS;
    callback_record = record_pointer;

    // set up callback
    if (incoming_active_callback != NULL) {
        return SB_PORT_EBUSY;
    }
    incoming_active_callback = signpost_storage_write_callback;

    // allocate new message buffer
    size_t logname_len = strnlen(record_pointer->logname, STORAGE_LOG_LEN);

    // send message
    int err = signpost_api_send(ModuleAddressStorage, CommandFrame,
            StorageApiType, StorageDeleteMessage, logname_len, (uint8_t*) record_pointer->logname); if (err < SB_PORT_SUCCESS) {
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
        return SB_PORT_ENOMEM;
    }

    buf[0] = slen;
    memcpy(buf+1, topic, slen);
    buf[slen+1] = data_len;
    memcpy(buf+1+slen+1, data, data_len);


    incoming_active_callback = signpost_networking_callback;
    networking_ready = false;
    int rc = signpost_api_send(ModuleAddressRadio, CommandFrame, NetworkingApiType,
                        NetworkingSendMessage, len, buf);

    free(buf);
    if(rc < SB_PORT_SUCCESS) {
        return rc;
    }

    rc = port_signpost_wait_for_with_timeout(&networking_ready, 3000);
    if(rc < SB_PORT_SUCCESS) {
        return rc;
    }

    if(incoming_message_length >= 4) {
        return *(int*)incoming_message;
    } else {
        return SB_PORT_FAIL;
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
    if(ret < 0) return SB_PORT_FAIL;

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
        return -SB_PORT_EBUSY;
    }
    if (energy_cb != NULL) {
        return -SB_PORT_EBUSY;
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

    return SB_PORT_SUCCESS;
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
        return SB_PORT_ENOMEM;
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
        return SB_PORT_FAIL;
    }


    // There is an integer in the incoming message that should be
    // sent back as the return code.
    if(energy_report_result < 0) {
        return SB_PORT_FAIL;
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
    if(rc < 0) return SB_PORT_FAIL;

    // There is an integer in the incoming message that should be
    // sent back as the return code.
    if(energy_reset_result < 0) {
        return SB_PORT_FAIL;
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
        return SB_PORT_EBUSY;
    }

    // Setup the callback that the API layer should use
    incoming_active_callback = timelocation_callback;

    // Call down to send the message
    int rc = signpost_api_send(ModuleAddressController,
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
        return SB_PORT_FAIL;
    }

    return timelocation_query_result;
}

int signpost_timelocation_get_time(signpost_timelocation_time_t* time) {
    // Check the argument, because why not.
    if (time == NULL) {
        return SB_PORT_EINVAL;
    }

    int rc = signpost_timelocation_sync(TimeLocationGetTimeMessage);
    if (rc < 0) return rc;

    // Do our due diligence
    if (incoming_message_length != sizeof(signpost_timelocation_time_t)) {
        SIGNBUS_DEBUG("Time message wrong length. Expected: %d, got %d\n",
            sizeof(signpost_timelocation_time_t), incoming_message_length);
        return SB_PORT_FAIL;
    }

    memcpy(time, incoming_message, incoming_message_length);

    return rc;
}

int signpost_timelocation_get_location(signpost_timelocation_location_t* location) {
    // Check the argument, because why not.
    if (location == NULL) {
        return SB_PORT_EINVAL;
    }

    int rc = signpost_timelocation_sync(TimeLocationGetLocationMessage);
    if (rc < 0) return rc;

    // Do our due diligence
    if (incoming_message_length != sizeof(signpost_timelocation_location_t)) {
        SIGNBUS_DEBUG("Location message wrong length. Expected: %d, got %d\n",
            sizeof(signpost_timelocation_location_t), incoming_message_length);
        return SB_PORT_FAIL;
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
