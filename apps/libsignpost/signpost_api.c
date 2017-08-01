#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "CRC16.h"
#include "erpc_client_setup.h"
#include "signbus_app_layer.h"
#include "signbus_io_interface.h"
#include "signpost_api.h"
#include "port_signpost.h"
#include "tock.h"

#include "mbedtls/ecdh.h"
#include "mbedtls/ecp.h"

#pragma GCC diagnostic ignored "-Wstack-usage="

static struct module_struct {
    uint8_t                 i2c_address;
    api_handler_t**         api_handlers;
    int8_t                  api_type_to_module_address[HighestApiType+1];
    uint8_t                 i2c_address_mods[NUM_MODULES];
    bool                    haskey[NUM_MODULES];
    uint8_t                 keys[NUM_MODULES][ECDH_KEY_LENGTH];
} module_info = {.i2c_address_mods = {ModuleAddressController, ModuleAddressStorage, ModuleAddressRadio}};


uint8_t* signpost_api_addr_to_key(uint8_t addr);
int      signpost_api_addr_to_mod_num(uint8_t addr);

// Translate module address to pairwise key
uint8_t* signpost_api_addr_to_key(uint8_t addr) {
    for (size_t i = 0; i < NUM_MODULES; i++) {
        if (addr == module_info.i2c_address_mods[i] && module_info.haskey[i]) {
            uint8_t* key = module_info.keys[i];
            printf("key: %p: 0x%02x%02x%02x...%02x\n", key,
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
    printf("WARN: Do not have module registered to address 0x%x\n", addr);
    return SB_PORT_FAIL;
}

int signpost_api_error_reply(uint8_t destination_address,
        signbus_api_type_t api_type, uint8_t message_type) {
    return signpost_api_send(destination_address,
            ErrorFrame, api_type, message_type, 0, NULL);
}

void signpost_api_error_reply_repeating(uint8_t destination_address,
        signbus_api_type_t api_type, uint8_t message_type,
        bool print_warnings, bool print_on_first_send, unsigned tries) {
   int rc;
   if (print_warnings && print_on_first_send) {
      printf(" - Sending API Error reply to 0x%02x for api 0x%02x and message 0x%02x.\n",
            destination_address, api_type, message_type);
   }
   do {
      rc = signpost_api_error_reply(destination_address, api_type, message_type);
      if (rc < 0) {
         tries--;
         printf(" - Error sending API Error reply to 0x%02x (code: %d).\n",
               destination_address, rc);
         printf(" - Sleeping 1s. Tries remaining %d\n", tries);
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
/* INCOMING MESSAGE / ASYNCHRONOUS DISPATCH                                */
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
        printf("%s:%d UNKNOWN ERROR %d\n", __FILE__, __LINE__, rc);
        printf("*** NO MORE MESSAGES WILL BE RECEIVED ***\n");
    }
}

static void signpost_api_recv_callback(int len_or_rc) {
    SIGNBUS_DEBUG("len_or_rc %d\n", len_or_rc);

    if (len_or_rc < 0) {
        if (len_or_rc == -94) {
            // These return codes are a hack
            printf("Dropping message with HMAC/HASH failure\n");
            signpost_api_start_new_async_recv();
        } else {
            printf("%s:%d It's all fubar?\n", __FILE__, __LINE__);
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
            printf("Warn: Unsolicited message for api %d. Dropping\n", incoming_api_type);
        }
    } else if ( (incoming_frame_type == ResponseFrame) || (incoming_frame_type == ErrorFrame) ) {
        if (incoming_active_callback != NULL) {
            // clear this before passing it on
            signbus_app_callback_t* temp = incoming_active_callback;
            incoming_active_callback = NULL;
            temp(len_or_rc);
        } else {
            printf("Warn: Unsolicited response/error. Dropping\n");
        }
    } else {
        printf("Invalid frame type: %d. Dropping message\n", incoming_frame_type);
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

    printf("INIT: Requested I2C isolation with controller\n");
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

        //printf("failed to read public parameters\n");
        return SB_PORT_FAIL;
    }

    uint8_t  module_number = signpost_api_addr_to_mod_num(incoming_source_address);
    if (module_number == 0xff) return SB_PORT_FAIL;
    uint8_t* key = module_info.keys[module_number];
    size_t keylen;
    // generate key
    if(mbedtls_ecdh_calc_secret(&ecdh, &keylen, key, ECDH_KEY_LENGTH,
                mbedtls_ctr_drbg_random, &ctr_drbg_context) < 0) {
        //printf("failed to calculate secret\n");
        return SB_PORT_FAIL;
    }
    module_info.haskey[signpost_api_addr_to_mod_num(incoming_source_address)] =
        true;

    SIGNBUS_DEBUG("key: %p: 0x%02x%02x%02x...%02x\n", key,
            key[0], key[1], key[2], key[ECDH_KEY_LENGTH-1]);

    printf("INIT: Initialization with module %d complete\n", signpost_api_addr_to_mod_num(incoming_source_address));
    return 0;
}

int signpost_initialization_key_exchange_send(uint8_t destination_address) {
    int rc;
    printf("INIT: Granted I2C isolation and started initialization with module %d\n", signpost_api_addr_to_mod_num(destination_address));
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

    printf("INIT: Registered address 0x%x as module %d\n", source_address, module_number);
    // Just ack, eventually will send new address
    return signpost_api_send(source_address, ResponseFrame, InitializationApiType, InitializationDeclare, 1, &module_number);
}
int signpost_initialization_key_exchange_respond(uint8_t source_address, uint8_t* ecdh_params, size_t len) {
    int ret = SB_PORT_SUCCESS;

    printf("INIT: Performing key exchange with module %d\n", signpost_api_addr_to_mod_num(source_address));
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
    rc = port_rng_init();
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
              printf("INIT: Timed out waiting for controller isolation\n");
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
              printf("INIT: Timed out waiting for controller declare response\n");
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
              printf("INIT: Timed out waiting for controller key exchange response\n");
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

static void signpost_storage_callback(int len_or_rc) {

    if (len_or_rc < SB_PORT_SUCCESS) {
        // error code response
        storage_result = len_or_rc;
    } else if (len_or_rc != sizeof(Storage_Record_t)) {
        // invalid response length
        printf("%s:%d - Error: bad len, got %d, want %d\n",
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

int signpost_storage_write (uint8_t* data, size_t len, Storage_Record_t* record_pointer) {
    storage_ready = false;
    storage_result = SB_PORT_SUCCESS;
    callback_record = record_pointer;

    // set up callback
    if (incoming_active_callback != NULL) {
        return SB_PORT_EBUSY;
    }
    incoming_active_callback = signpost_storage_callback;

    // send message
    int err = signpost_api_send(ModuleAddressStorage, CommandFrame,
            StorageApiType, StorageWriteMessage, len, data); if (err < SB_PORT_SUCCESS) {
        return err;
    }

    // wait for response
    yield_for(&storage_ready);
    return storage_result;
}

int signpost_storage_write_reply(uint8_t destination_address, uint8_t* record_pointer) {
    return signpost_api_send(destination_address,
            ResponseFrame, StorageApiType, StorageWriteMessage,
            sizeof(Storage_Record_t), record_pointer);
}

/**************************************************************************/
/* PROCESSING API                                                         */
/**************************************************************************/
static bool processing_ready;
static void signpost_processing_callback(__attribute__((unused)) int result){
    processing_ready = true;
}

int signpost_processing_init(const char* path) {
    erpc_client_init(NULL);

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
    yield_for(&processing_ready);

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
    yield_for(&processing_ready);

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

    yield_for(&processing_ready);

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
static void signpost_networking_post_callback(int result) {
    networking_ready = true;
    networking_result = result;
}

int signpost_networking_send_bytes(uint8_t destination_address, uint8_t* data, uint16_t data_len) {
    return signpost_api_send(destination_address,
            NotificationFrame, NetworkingApiType, NetworkingSend,
            data_len, data);
}

int signpost_networking_post(const char* url, http_request request, http_response* response) {

    //form the sending array
    uint16_t header_size = 0;
    for(uint8_t i = 0; i < request.num_headers; i++) {
        header_size += strlen(request.headers[i].header);
        header_size += strlen(request.headers[i].value);
        header_size += 2;
    }
    uint16_t message_size = request.body_len + strlen(url) + header_size + 6 + 25;

    //this is the max message size
    //You probably can't get a stack big enough for this to work
    //but if you could, we are stopping at 15000 because out networking stack
    //is uint16_t size limited
    if(message_size > 15000) {
        //this is an error - we can't send this
        return -1;
    }

    //we need to serialize the post structure
    uint8_t send[message_size];
    uint16_t send_index = 0;
    uint16_t len = strlen(url);
    //first length of url
    send[0] = (len & 0x00ff);
    send[1] = ((len & 0xff00) >> 8);
    send_index += 2;
    //then url
    memcpy(send+send_index,url,len);
    send_index += len;
    //number of headers
    uint16_t num_headers_index = send_index;
    send[send_index] = request.num_headers;
    send_index++;
    bool has_content_length = false;

    //pack headers
    //len_key
    //key
    //len_value
    //value
    for(uint8_t i = 0; i < request.num_headers; i++) {
        uint8_t f_len = strlen(request.headers[i].header);
        send[send_index] = f_len;
        send_index++;
        if(!strcasecmp(request.headers[i].header,"content-length")) {
            has_content_length = true;
        }
        memcpy(send+send_index,request.headers[i].header,f_len);
        send_index += f_len;
        f_len = strlen(request.headers[i].value);
        send[send_index] = f_len;
        send_index++;
        memcpy(send+send_index,request.headers[i].value,f_len);
        send_index += f_len;
    }
    len = request.body_len;

    //add a content length header if the sender doesn't
    if(!has_content_length) {
        send[num_headers_index]++;
        uint8_t clen = strlen("content-length");
        send[send_index] = clen;
        send_index++;
        memcpy(send+send_index,(uint8_t*)"content-length",clen);
        send_index += clen;
        char cbuf[5];
        snprintf(cbuf,5,"%d",len);
        clen = strnlen(cbuf,5);
        send[send_index] = clen;
        send_index++;
        memcpy(send+send_index,cbuf,clen);
        send_index += clen;
    }

    //body len
    send[send_index] = (len & 0x00ff);
    send[send_index + 1] = ((len & 0xff00) >> 8);
    send_index += 2;
    //body
    memcpy(send + send_index, request.body, request.body_len);
    send_index += len;

    //setup the response callback
    incoming_active_callback = signpost_networking_post_callback;
    networking_ready = false;

    //call app_send
    int rc;
    rc = signpost_api_send(ModuleAddressRadio,  CommandFrame,
            NetworkingApiType, NetworkingPostMessage, send_index + 1, send);
    if (rc < 0) return rc;

    //wait for a response
    yield_for(&networking_ready);

    //parse the response
    //deserialize
    //Note, we just fill out to the max_lens that the app has provided
    //and drop the rest
    uint8_t *b = incoming_message;
    uint16_t i = 0;
    //status
    response->status = b[i] + (((uint16_t)b[i+1]) >> 8);
    i += 2;
    //reason_len
    uint16_t reason_len = b[i] + (((uint16_t)b[i+1]) >> 8);
    i += 2;
    //reason
    if(reason_len < response->reason_len) {
        response->reason_len = reason_len;
        memcpy(response->reason,b+i,reason_len);
        i += reason_len;
    } else {
        memcpy(response->reason,b+i,response->reason_len);
        i += response->reason_len;
    }
    //
    uint8_t num_headers = b[i];
    i += 1;
    uint8_t min;
    if(num_headers < response->num_headers) {
        min = num_headers;
        response->num_headers = num_headers;
    } else {
        min = response->num_headers;
    }
    for(uint8_t j = 0; j < min; j++) {
        uint8_t hlen = b[i];
        i += 1;
        if(hlen < response->headers[j].header_len) {
            response->headers[j].header_len = hlen;
            memcpy(response->headers[j].header,b + i,hlen);
        } else {
            memcpy(response->headers[j].header,b + i,response->headers[j].header_len);
        }
        i += hlen;
        hlen = b[i];
        i += 1;
        if(hlen < response->headers[j].value_len) {
            response->headers[j].value_len = hlen;
            memcpy(response->headers[j].value,b + i,hlen);
        } else {
            memcpy(response->headers[j].value,b + i ,response->headers[j].value_len);
        }
        i += hlen;
    }
    //we need to jump to the spot the body begins if we didn't
    //go through all the headers
    if(min < num_headers) {
        for(uint8_t j = 0; j < num_headers-min; j++) {
            uint8_t hlen = b[i];
            i += 1;
            i += hlen;
            hlen = b[i];
            i += 1;
            i += hlen;
        }
    }
    uint16_t body_len = b[i] + (((uint16_t)b[i+1]) >> 8);
    i += 2;
    if(body_len < response->body_len) {
        response->body_len = body_len;
        memcpy(response->body,b+i,body_len);
    } else {
        memcpy(response->body,b+i,response->body_len);
    }

    return 0;
}

void signpost_networking_post_reply(uint8_t src_addr, uint8_t* response,
                                    uint16_t response_len) {
   int rc;
   rc = signpost_api_send(src_addr, ResponseFrame, NetworkingApiType,
                        NetworkingPostMessage, response_len, response);
   if (rc < 0) {
      printf(" - %d: Error sending POST reply (code: %d)\n", __LINE__, rc);
      signpost_api_error_reply_repeating(src_addr, NetworkingApiType,
            NetworkingPostMessage, true, true, 1);
   }
}

/**************************************************************************/
/* ENERGY API                                                             */
/**************************************************************************/

static bool energy_query_ready;
static int  energy_query_result;
static signbus_app_callback_t* energy_cb = NULL;
static signpost_energy_information_t* energy_cb_data = NULL;

static void energy_query_sync_callback(int result) {
    SIGNBUS_DEBUG("result %d\n", result);
    energy_query_ready = true;
    energy_query_result = result;
}

int signpost_energy_query(signpost_energy_information_t* energy) {
    energy_query_ready = false;

    {
        int rc = signpost_energy_query_async(energy, energy_query_sync_callback);
        if (rc != 0) {
            return rc;
        }
    }

    yield_for(&energy_query_ready);

    return energy_query_result;
}

static void energy_query_async_callback(int len_or_rc) {
    SIGNBUS_DEBUG("len_or_rc %d\n", len_or_rc);

    if (len_or_rc != sizeof(signpost_energy_information_t)) {
        printf("%s:%d - Error: bad len, got %d, want %d\n",
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
    if (rc < 0) return rc;

    return SB_PORT_SUCCESS;
}

int signpost_energy_query_reply(uint8_t destination_address,
        signpost_energy_information_t* info) {
    return signpost_api_send(destination_address,
            ResponseFrame, EnergyApiType, EnergyQueryMessage,
            sizeof(signpost_energy_information_t), (uint8_t*) info);
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
    yield_for(&timelocation_query_answered);

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

    yield_for(&watchdog_reply);

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

    yield_for(&watchdog_reply);

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

/**************************************************************************/
/* JSON API                                                               */
/**************************************************************************/

// TODO figure out actual size needed
#define MAX_JSON_BLOB_SIZE 1000
static char json_blob[MAX_JSON_BLOB_SIZE];

int signpost_json_send(uint8_t destination_address, size_t field_count, ... ) {
    size_t size = 0;
    json_field_t field;
    va_list args;

    // expecting "field name" + value
    va_start(args, field_count);
    json_blob[size++] = '{';
    for(size_t i = 0; i < field_count; i++){
        if(size >= MAX_JSON_BLOB_SIZE) {
            return SB_PORT_ESIZE;
        }
        if(i!=0) {
           json_blob[size++] = ',';
        }
        // get field
        field = va_arg(args, json_field_t);
        // field name:
        json_blob[size++] = '"';
        strcpy(json_blob+size, field.name);
        size+=strlen(json_blob+size);
        json_blob[size++] = '"';
        json_blob[size++] = ':';
        // value:
        // TODO assumes integer value right now
        snprintf(json_blob+size, MAX_JSON_BLOB_SIZE-size, "%d", field.value);
        size+=strnlen(json_blob+size,MAX_JSON_BLOB_SIZE-size);
    }
    json_blob[size++] = '}';
    json_blob[size++] = '\0';
    return signpost_api_send(destination_address, NotificationFrame,
            NetworkingApiType, NetworkingSend, size, (uint8_t*)json_blob);
}

/*************************************************************************/
/* signpost  update */
/*************************************************************************/

bool update_message;
static void update_callback(__attribute__ ((unused)) int result) {
    update_message = true;
}

//this will fetch the update and perform it
int signpost_update(char* url,
                    char* version_string,
                    uint32_t flash_scratch_start,
                    uint32_t flash_scratch_length,
                    uint32_t flash_dest_address) {

    uint32_t update_len;
    uint16_t crc;
    int ret = signpost_fetch_update(url, version_string, flash_scratch_start,
                                         flash_scratch_length, &update_len,
                                         &crc);

    if(ret == UpdateFetched) {
        ret = signpost_apply_update(flash_dest_address,
                                    flash_scratch_start, update_len, crc);
    }

    return ret;
}

//this gives the user a bit more control to fetch, then decide when to apply
int signpost_fetch_update(char* url,
                            char* version_string,
                            uint32_t flash_scratch_start,
                            uint32_t flash_scratch_length,
                            uint32_t* update_length,
                            uint16_t* crc) {

    //first pack the initial update request to the radio
    uint32_t url_len = strlen(url);
    uint32_t version_len = strlen(version_string);
    char* message = malloc(url_len+version_len+8);
    if(!message) {
        return SB_PORT_ENOMEM;
    }

    memcpy(message,&url_len,4);
    memcpy(message+4,url,url_len);
    memcpy(message+4+url_len,&version_len,4);
    memcpy(message+8+url_len,version_string,version_len);

    int ret = signpost_api_send(ModuleAddressRadio,
            CommandFrame, UpdateApiType, UpdateRequestMessage,
            url_len+version_len+8, (uint8_t*) message);

    free(message);

    while(true) {
        update_message = false;
        incoming_active_callback = update_callback;
        int rc = port_signpost_wait_for_with_timeout(&update_message, 60000);
        if(rc < SB_PORT_SUCCESS) {
            //timeout
            return SB_PORT_FAIL;
        }

        //we got the message, is it a done or a xfer message?
        if(incoming_message_type == UpdateTransferMessage) {
            //take the message, unpack it, do a flash write, then respond
            uint32_t data_len;
            uint32_t offset;

            memcpy(&data_len, incoming_message, 4);
            uint8_t* data = malloc(data_len);
            if(!data) {
                return SB_PORT_ENOMEM;
            }
            memcpy(data, incoming_message+4, data_len);
            memcpy(&offset, incoming_message+4+data_len, 4);

            if(offset+data_len > flash_scratch_length) {
                return SB_PORT_ENOMEM;
            }

            ret = port_signpost_flash_write(flash_scratch_start + offset, data, data_len);

            free(data);
            if(ret < 0) return ret;

            //send response message so radio can proceed
            ret = signpost_api_send(ModuleAddressRadio,
                ResponseFrame, UpdateApiType, UpdateResponseMessage,
                0, NULL);

            if(ret < 0) return ret;
        } else if (incoming_message_type == UpdateResponseMessage) {
            signpost_update_done_t resp;
            memcpy(&resp, incoming_message, sizeof(signpost_update_done_t));

            if(resp.response_code == UpdateUpToDate) {
                return UpdateUpToDate;
            } else if(resp.response_code == UpdateFetched) {
                //copy the crc and length and return update fetched
                memcpy(&update_length, &resp.total_length, 4);
                memcpy(&crc, &resp.crc, 2);

                return UpdateFetched;
            } else {
                return resp.response_code;
            }
        } else {
            return SB_PORT_FAIL;
        }
    }
}

//this is the apply function for the fetch counterpart
int signpost_apply_update(uint32_t flash_dest_address,
                            uint32_t flash_scratch_start,
                            uint32_t update_length,
                            uint16_t crc) {

    int ret = port_signpost_apply_update(flash_dest_address,
                                         flash_scratch_start,
                                         update_length,
                                         crc);
    if(ret < 0) {
        return UpdateApplyFailure;
    } else {
        return UpdateApplied;
    }
}

int signpost_update_transfer_reply(uint8_t dest_addr, uint8_t* binary_chunk, uint32_t offset, uint32_t len) {

    uint8_t* message = malloc(len+8);
    if(!message) {
        return SB_PORT_ENOMEM;
    }

    memcpy(message, &len, 4);
    memcpy(message+4, binary_chunk, len);
    memcpy(message+4+len, &offset, 4);

    int ret = signpost_api_send(dest_addr,
       ResponseFrame, UpdateApiType, UpdateTransferMessage,
       8+len, message);

    free(message);

    return ret;
}

int signpost_update_done_reply(uint8_t dest_addr, uint32_t response_code, uint16_t crc) {
    signpost_update_done_t rep;
    rep.response_code = response_code;
    rep.crc = crc;

    int ret = signpost_api_send(dest_addr,
       ResponseFrame, UpdateApiType, UpdateResponseMessage,
       sizeof(signpost_update_done_t), (uint8_t*) &rep);

    return ret;
}


