#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "crc.h"
#include "strnstr.h"

//nordic includes
#include "nrf.h"
#include <nordic_common.h>
#include <nrf_error.h>
#include <simple_ble.h>
#include <eddystone.h>
#include <simple_adv.h>
//#include "multi_adv.h"

//tock includes
#include <signpost_api.h>
#include "tock.h"
#include "console.h"
#include "timer.h"
#include "xdot.h"
#include "sara_u260.h"
#include "i2c_master_slave.h"
#include "app_watchdog.h"
#include "radio_module.h"
#include "gpio.h"
#include "RadioDefs.h"
#include "CRC16.h"
#include "led.h"

//definitions for the ble
#define DEVICE_NAME "Signpost"
#define PHYSWEB_URL "j2x.us/signp"
#define POST_ADDRESS "ec2-35-166-179-172.us-west-2.compute.amazonaws.com"

#define UMICH_COMPANY_IDENTIFIER 0x02E0

uint16_t conn_handle = BLE_CONN_HANDLE_INVALID;

static simple_ble_config_t ble_config = {
    .platform_id        = 0x00,
    .device_id          = DEVICE_ID_DEFAULT,
    .adv_name           = DEVICE_NAME,
    .adv_interval       = MSEC_TO_UNITS(300, UNIT_0_625_MS),
    .min_conn_interval  = MSEC_TO_UNITS(500, UNIT_1_25_MS),
    .max_conn_interval  = MSEC_TO_UNITS(1250, UNIT_1_25_MS),
};

// short uuid is 0x6F00
#define EVENTUAL_BUFFER_SIZE 200
static simple_ble_service_t signpost_service = {
    .uuid128 = {{0x5C, 0xC2, 0x0D, 0x14, 0x6D, 0x28, 0x49, 0x7A,
                 0x8F, 0x56, 0x66, 0xB7, 0x6F, 0x00, 0xE9, 0x75}}
};
static simple_ble_char_t log_update_char = {.uuid16 = 0x6F01};
static simple_ble_char_t log_read_char = {.uuid16 = 0x6F02};
static simple_ble_char_t log_notify_char = {.uuid16 = 0x6F03};
static uint8_t log_update_value [STORAGE_LOG_LEN];
static uint8_t log_buffer[EVENTUAL_BUFFER_SIZE];
static size_t  log_bytes_remaining;

//definitions for the i2c
#define BUFFER_SIZE 100
#define ADDRESS_SIZE 6
#define NUMBER_OF_MODULES 8
#define CELL_POST_SIZE 2000

//array of the data we're going to send on the radios
//make a queue of 30 deep
#define QUEUE_SIZE 30
uint8_t data_queue[QUEUE_SIZE][BUFFER_SIZE];
uint8_t data_length[QUEUE_SIZE];
uint8_t data_address[QUEUE_SIZE];
uint8_t queue_head = 0;
uint8_t queue_tail = 0;

// Records of stored data to send eventually
// for now, just use queue size
#define EVENTUAL_REC_SIZE 10
size_t num_saved_records = 0;
size_t selected_record = 0;
Storage_Record_t saved_records[EVENTUAL_REC_SIZE] = {0};

uint32_t lora_packets_sent = 1;
uint8_t module_num_map[NUMBER_OF_MODULES] = {0};
uint8_t number_of_modules = 0;
uint8_t module_packet_count[NUMBER_OF_MODULES] = {0};
uint8_t status_send_buf[400] = {0};
uint8_t status_length_offset = 0;
uint8_t status_data_offset = 0;

#define LORA_JOINED 0
#define LORA_ERROR -1
int lora_state = LORA_ERROR;

static uint8_t seq_num = 0;
static bool currently_sending = false;

static void increment_queue_pointer(uint8_t* p) {
    if(*p == (QUEUE_SIZE -1)) {
        *p = 0;
    } else {
        (*p)++;
    }
}

#ifndef COMPILE_TIME_ADDRESS
#error Missing required define COMPILE_TIME_ADDRESS of format: 0xC0, 0x98, 0xE5, 0x12, 0x00, 0x00
#endif
static uint8_t address[ADDRESS_SIZE] = { COMPILE_TIME_ADDRESS };

static int join_lora_network(void);

/*static void adv_config_data(void) {
    static uint8_t i = 0;

    static ble_advdata_manuf_data_t mandata;

    if(data_to_send[i][0] != 0x00) {
        mandata.company_identifier = UMICH_COMPANY_IDENTIFIER;
        mandata.data.p_data = data_to_send[i];
        mandata.data.size = BUFFER_SIZE;

        simple_adv_manuf_data(&mandata);
    }

    i++;
    if(i >= NUMBER_OF_MODULES) {
        i = 0;
    }
}*/

static void count_module_packet(uint8_t module_address) {

    if(module_address == 0x0) {
        //this was an error
        return;
    }

    for(uint8_t i = 0; i < NUMBER_OF_MODULES; i++) {

        if(module_num_map[i] == 0) {
            module_num_map[i] = module_address;
            module_packet_count[i]++;
            number_of_modules++;
            break;
        } else if(module_num_map[i] == module_address) {
            module_packet_count[i]++;
            break;
        }
    }
}

static int8_t add_buffer_to_queue(uint8_t addr, uint8_t* buffer, uint8_t len) {
    uint8_t temp_tail = queue_tail;
    increment_queue_pointer(&temp_tail);
    if(temp_tail == queue_head) {
        return TOCK_FAIL;
    } else {
        if(len <= BUFFER_SIZE) {
            data_address[queue_tail]= addr;
            memcpy(data_queue[queue_tail], buffer, len);
            data_length[queue_tail] = len;
            increment_queue_pointer(&queue_tail);
        } else {
            return TOCK_ENOMEM;
        }
        return 0;
    }
}

static int add_message_to_buffer(uint8_t *buffer, size_t buf_len, size_t* offset, uint8_t *data, size_t len) {
  if (len + 2 > buf_len) return TOCK_FAIL;
  // lengths are only 1 byte, but header takes 2 bytes
  buffer[*offset] = 0;
  buffer[*offset+1] = (uint8_t) (len & 0xff);
  *offset += 2;

  memcpy(buffer+*offset, data, len);
  *offset += len;

  return TOCK_SUCCESS;
}


static int save_eventual_buffer(uint8_t* buffer, size_t len) {
    int rc = 0;
    size_t topic_len = buffer[0];
    char* topic = (char*) (buffer + 1);
    printf("\ngot save request!\n");
    for (size_t i = 0; i < topic_len; i++) {
      printf("%c", topic[i]);
    }
    printf("\n");
    if (topic_len >= len) return TOCK_ESIZE; // impossible topic_length
    size_t data_len = buffer[topic_len + 1];
    if (topic_len + data_len + 2 != len) return TOCK_ESIZE; // incorrect length
    //uint8_t* data = buffer + topic_len + 2;

    // search for existing records with same topic
    Storage_Record_t* store_record = NULL;
    for (int i = 0; i < EVENTUAL_REC_SIZE; i++) {
      if (strncmp(saved_records[i].logname, topic, topic_len) == 0) {
        store_record = &saved_records[i];
        break;
      }
    }

    uint8_t store_buf[200] = {0};
    size_t offset = 0;

    // Create new record, and set up log
    if (store_record == NULL) {
      if(num_saved_records == EVENTUAL_REC_SIZE) {
        // record keeping is full
        return TOCK_FAIL;
      }
      //itoa(addr, saved_records[num_saved_records].logname, 16);
      //saved_records[num_saved_records].logname[2] = '_';

      //XXX search for empty, available spaces
      memcpy(saved_records[num_saved_records].logname, topic, topic_len);
      saved_records[num_saved_records].offset = 0;
      saved_records[num_saved_records].length = 0;
      store_record = &saved_records[num_saved_records];
      num_saved_records += 1;
    }

    // use temp_record and ignore write_storage modifies to record
    // This is a workaround of the async nature of write_storage, and the
    // inability to wait within a callback
    Storage_Record_t temp_record = {};
    memcpy(&temp_record, store_record, sizeof(Storage_Record_t));

    // add the message to the buffer
    rc = add_message_to_buffer(store_buf, 100, &offset, buffer, len);
    if (rc < 0) {
      return rc;
    }
    //for (int i = 0; i < offset; i++) {
    //  printf("%02x", store_buf[i]);
    //}
    //printf("\n");
    //printf("\n");
    // Append message to log
    rc = signpost_storage_write(store_buf, offset, &temp_record);
    store_record->length += offset;

    return rc;
}

static uint8_t calc_queue_length(void) {
    //calculate and add the queue size in the status packet
    if(queue_tail >= queue_head) {
        return queue_tail-queue_head;
    } else {
        return QUEUE_SIZE-(queue_head-queue_tail);
    }
}

static void networking_api_callback(uint8_t source_address,
        signbus_frame_type_t frame_type, __attribute ((unused)) signbus_api_type_t api_type,
        uint8_t message_type, size_t message_length, uint8_t* message) {

    if (frame_type == NotificationFrame || frame_type == CommandFrame) {
        //printf("Got message!\n");
        if(message_type == NetworkingSendMessage) {
            int rc = add_buffer_to_queue(source_address, message, message_length);
            signpost_networking_send_reply(source_address, NetworkingSendMessage, rc);
        }
        else if( message_type == NetworkingSendEventuallyMessage) {
            int rc = save_eventual_buffer(message, message_length);
            signpost_networking_send_reply(source_address, NetworkingSendEventuallyMessage, rc);
        }
    }
}

static int find_end_of_response_header(void) {
    //okay now parse the version file
    //first loop through looking for the end of the header
    bool header_done = false;
    char read_buf[200];
    uint32_t offset = 0;
    while(!header_done) {
        printf("Reading 200 bytes\n");
        int ret = sara_u260_get_get_partial_response((uint8_t*)read_buf, offset, 200);
        if(ret < SARA_U260_SUCCESS) {
            printf("Byte read error code %d\n", ret);
            return ret;
        }

        //find if the end string is in this string
        char* pt = strnstr(read_buf,"\r\n\r\n",200);
        if(pt != NULL) {
            offset += (pt-read_buf);
            header_done = true;
            printf("Found end of header at byte %lu!\n",offset);
            break;
        }

        //make sure we didn't just get part of it
        if(!strncmp(read_buf+199,"\r",1) || !strncmp(read_buf+198,"\r\n",2) || !strncmp(read_buf+197,"\r\n\r",3)) {
            //we could have gotten part of it, shift a bit and read again
            printf("We might have split off offset - read again\n");
            offset += 4;
        } else {
            printf("Nothing here on to the next read\n");
            offset += 200;
        }

        if(ret < 200 && header_done == false) {
            printf("End of file without finding anything\n");
            //we got to the end and didn't find the end of the header
            return SARA_U260_ERROR;
        }
    }

    return offset;
}

#define WAITING_FOR_UPDATE 0
#define TRANSFERRING_BINARY 1
#define CHECKING_UPDATE 2

static void update_api_callback(uint8_t source_address,
        signbus_frame_type_t frame_type, __attribute ((unused)) signbus_api_type_t api_type,
        uint8_t message_type, size_t message_length, uint8_t* message) {

    static int update_state = WAITING_FOR_UPDATE;
    static bool done_transferring = false;
    static uint32_t crc;
    static uint32_t len;
    static uint32_t offset;
    static uint32_t binary_offset;

    if(frame_type == CommandFrame) {
        printf("Received update request from 0x%02x\n", source_address);

        if(message_type == UpdateRequestMessage && update_state == WAITING_FOR_UPDATE) {
            update_state = CHECKING_UPDATE;

            //parse the request
            uint32_t url_len;
            uint32_t version_len;
            char* url;
            char* version;
            if(message_length >= 4) {
                memcpy(&url_len, message, 4);
            } else {
                printf("UPDATE ERROR - update message too short\n");
                signpost_update_error_reply(source_address);
                update_state = WAITING_FOR_UPDATE;
                return;
            }

            if(message_length >= 8 + url_len) {
                memcpy(&version_len, message+4+url_len, 4);
            } else {
                printf("UPDATE ERROR - update message too short\n");
                signpost_update_error_reply(source_address);
                update_state = WAITING_FOR_UPDATE;
                return;
            }

            printf("Update: url_len: %lu version_len: %lu\n",url_len,version_len);

            if(message_length >= 8 + url_len + version_len) {
                url = malloc(url_len+5);
                memset(url, 0, url_len+5);
                if(!url) {
                    printf("UPDATE ERROR: No memory for url of len %lu\n",url_len);
                    signpost_update_error_reply(source_address);
                    update_state = WAITING_FOR_UPDATE;
                    return;
                }

                version = malloc(version_len+5);
                memset(version, 0, version_len+5);
                if(!version) {
                    free(url);
                    printf("UPDATE ERROR: No memory for verion of len %lu\n",version_len);
                    signpost_update_error_reply(source_address);
                    update_state = WAITING_FOR_UPDATE;
                    return;
                }

                memcpy(url, message+4, url_len);
                memcpy(version, message+8+url_len, version_len);
            } else {
                printf("UPDATE ERROR - update message too short\n");
                signpost_update_error_reply(source_address);
                update_state = WAITING_FOR_UPDATE;
                return;
            }

            printf("Update URL: %.*s\n",(int)url_len,url);
            printf("Update Version: %.*s\n",(int)version_len,version);

            //find the first slash in the url
            uint32_t slash = 0;
            for(uint32_t i = 0; i < url_len; i++) {
                if(url[i] == '/') {
                    slash = i;
                    break;
                }
            }

            char* pre_slash = malloc(slash + 1);
            if(!pre_slash)  {
                free(url);
                free(version);
                signpost_update_error_reply(source_address);
                update_state = WAITING_FOR_UPDATE;
                return;
            }

            char* post_slash_version = malloc(url_len - slash+10);
            if(!post_slash_version) {
                free(url);
                free(version);
                free(pre_slash);
                signpost_update_error_reply(source_address);
                update_state = WAITING_FOR_UPDATE;
                return;
            }

            char* post_slash_app = malloc(url_len - slash+10);
            if(!post_slash_app) {
                free(url);
                free(version);
                free(pre_slash);
                signpost_update_error_reply(source_address);
                update_state = WAITING_FOR_UPDATE;
                return;
            }

            snprintf(pre_slash, slash+1, "%s", url);
            if(url[url_len] == '/') {
                snprintf(post_slash_version, url_len - slash + 10, "%sinfo.txt", url+slash);
                snprintf(post_slash_app, url_len - slash + 10, "%sapp.bin", url+slash);
            } else {
                snprintf(post_slash_version, url_len - slash + 10, "%s/info.txt", url+slash);
                snprintf(post_slash_app, url_len - slash + 10, "%s/app.bin", url+slash);
            }
            printf("Update URL: %s\n",pre_slash);
            printf("Update Info Path: %s\n",post_slash_version);
            printf("Update App Path: %s\n",post_slash_app);

            //send the http get for the version file
            int ret = sara_u260_basic_http_get(pre_slash,post_slash_version);
            free(post_slash_version);

            if(ret < SARA_U260_SUCCESS) {
                printf("UPDATE: HTTP get of version info failed with error %d\n",ret);
                free(version);
                free(pre_slash);
                free(post_slash_app);
                signpost_update_error_reply(source_address);
                update_state = WAITING_FOR_UPDATE;
                return;
            }

            ret = find_end_of_response_header();
            if(ret < SARA_U260_SUCCESS) {
                printf("UPDATE: finding end of header failed with error %lu\n",offset);
                free(version);
                free(pre_slash);
                free(post_slash_app);
                signpost_update_error_reply(source_address);
                update_state = WAITING_FOR_UPDATE;
                return;
            }
            offset = ret;
            //move past the newline series
            offset += 4;
            printf("UPDATE: Header ends at position %lu\n",offset);

            //read the body of the file
            char read_buf[200];
            ret = sara_u260_get_get_partial_response((uint8_t*)read_buf, offset, 200);
            if(ret == 200) {
                printf("UPDATE ERROR: Info file too long\n");
                //the info file should not be this long
                free(version);
                free(pre_slash);
                free(post_slash_app);
                signpost_update_error_reply(source_address);
                update_state = WAITING_FOR_UPDATE;
                return;
            }
            printf("Got version file:\n%.*s\n",ret,read_buf);

            //parse the body of the file
            char* version_line = strnstr(read_buf,"\n",ret);
            if(!version_line) {
                free(version);
                free(pre_slash);
                free(post_slash_app);
                signpost_update_error_reply(source_address);
                update_state = WAITING_FOR_UPDATE;
                return;
            }

            char* len_line = strnstr(version_line+1,"\n",ret - (version_line-read_buf));
            if(!len_line) {
                free(version);
                free(pre_slash);
                free(post_slash_app);
                signpost_update_error_reply(source_address);
                update_state = WAITING_FOR_UPDATE;
                return;
            }

            char* crc_line = strnstr(len_line+1,"\n",ret - (len_line-read_buf));
            if(!len_line) {
                free(version);
                free(pre_slash);
                free(post_slash_app);
                signpost_update_up_to_date_reply(source_address);
                update_state = WAITING_FOR_UPDATE;
                return;
            }

            //compare the version
            printf("Read version of %.*s\n",(version_line-read_buf),read_buf);
            int cmp = strncmp(read_buf, version, (version_line - read_buf));
            free(version);
            if(cmp <= 0) {
                //don't update because versions match
                printf("UPDATE ERROR: New version equal to or older than current version\n");
                free(pre_slash);
                free(post_slash_app);
                update_state = WAITING_FOR_UPDATE;
                return;
            }

            //extract the length and the CRC
            printf("CRC read as %.*s\n",crc_line-len_line,len_line+1);
            crc = strtoul(len_line+1,&crc_line,16);
            len = strtoul(version_line,&len_line,16);
            printf("UPDATE: Found CRC of 0x%08lx\n",crc);
            printf("UPDATE: Found length of 0x%08lx\n",len);


            //okay now fetch the actual update
            ret = sara_u260_basic_http_get(pre_slash,post_slash_app);
            free(pre_slash);
            free(post_slash_app);
            if(ret < SARA_U260_SUCCESS) {
                printf("UPDATE: HTTP get of binary failed with error %d\n",ret);
                signpost_update_error_reply(source_address);
                update_state = WAITING_FOR_UPDATE;
                return;
            }

            //find the end of the response
            ret = find_end_of_response_header();
            if(ret < SARA_U260_SUCCESS) {
                printf("UPDATE: finding end of header failed with error %lu\n",offset);
                signpost_update_error_reply(source_address);
                update_state = WAITING_FOR_UPDATE;
                return;
            }
            offset = ret;
            //again move past newlines
            offset += 4;

            //okay now we should move to the transferring state
            update_state = TRANSFERRING_BINARY;

            //now stream the binary back in chunks
            //
            //send the first chunk then return and send the rest from response frames
            done_transferring = false;
            binary_offset = 0;
            ret = sara_u260_get_get_partial_response((uint8_t*)read_buf, offset, 200);
            if(ret < SARA_U260_SUCCESS) {
                printf("Update ERROR getting partial response\n");
                update_state = WAITING_FOR_UPDATE;
                return;
            } else if (ret < 200) {
                //done getting responses
                printf("UPDATE: Transferring final with offset %lu\n",offset);
                signpost_update_transfer_reply(source_address,(uint8_t*)read_buf,binary_offset,ret);
                done_transferring = true;
            } else {
                printf("UPDATE: Transferring with offset %lu\n",offset);
                signpost_update_transfer_reply(source_address,(uint8_t*)read_buf,binary_offset,200);
                binary_offset += 200;
                offset += 200;
            }
        } else if(message_type == UpdateResponseMessage && update_state == TRANSFERRING_BINARY) {
            printf("UPDATE: Received ack to continue transfer\n");
            if(done_transferring) {
                printf("Sending update reply done\n");
                signpost_update_done_reply(source_address,UpdateFetched,len,crc);
                update_state = WAITING_FOR_UPDATE;
                return;
            } else {
                uint8_t read_buf[200];
                int ret = sara_u260_get_get_partial_response((uint8_t*)read_buf, offset, 200);
                if(ret < SARA_U260_SUCCESS) {
                    printf("Update ERROR getting partial response\n");
                    update_state = WAITING_FOR_UPDATE;
                    return;
                } else if (ret < 200) {
                    //done getting responses
                    printf("UPDATE: Transferring final with offset %lu\n",offset);
                    signpost_update_transfer_reply(source_address,(uint8_t*)read_buf,binary_offset,ret);
                    done_transferring = true;
                } else {
                    printf("UPDATE: Transferring with offset %lu\n",offset);
                    signpost_update_transfer_reply(source_address,(uint8_t*)read_buf,binary_offset,200);
                    binary_offset += 200;
                    offset += 200;
                }
            }
        }
    } else {
        return;
    }
}

__attribute__ ((const))
void services_init(void) {
    simple_ble_add_service(&signpost_service);

    simple_ble_add_stack_characteristic(1, 1, 0, 0,
        STORAGE_LOG_LEN, (uint8_t*) log_update_value,
        &signpost_service, &log_update_char);
    simple_ble_add_stack_characteristic(1, 0, 0, 0,
        EVENTUAL_BUFFER_SIZE, (uint8_t*) log_buffer,
        &signpost_service, &log_read_char);
    simple_ble_add_stack_characteristic(0, 0, 1, 0,
        sizeof(size_t), (uint8_t*) &log_bytes_remaining,
        &signpost_service, &log_notify_char);

}

__attribute__ ((const))
void ble_address_set(void) {
    static ble_gap_addr_t gap_addr;

    //switch the addres to little endian in a for loop
    for(uint8_t i = 0; i < ADDRESS_SIZE; i++) {
        gap_addr.addr[i] = address[ADDRESS_SIZE-1-i];
    }

    //set the address type
    gap_addr.addr_type = BLE_GAP_ADDR_TYPE_PUBLIC;
    uint32_t err_code = sd_ble_gap_address_set(BLE_GAP_ADDR_CYCLE_MODE_NONE, &gap_addr);
    APP_ERROR_CHECK(err_code);
}

void ble_error(uint32_t error_code __attribute__ ((unused))) {
    //this has to be here too
    printf("ble error, resetting...");
    //app_watchdog_reset_app();
}

static uint32_t simple_ble_stack_char_get_test (simple_ble_char_t* char_handle, uint16_t* len, uint8_t* buf) {
    ble_gatts_value_t value = {
        .len = *len,
        .offset = 0,
        .p_value = buf,
    };

    return sd_ble_gatts_value_get(conn_handle, char_handle->char_handle.value_handle, &value);
}

void ble_evt_write(ble_evt_t* p_ble_evt) {
  if(simple_ble_is_char_event(p_ble_evt, &log_update_char)) {
    uint16_t char_len = STORAGE_LOG_LEN;
    // get char from nrf
    simple_ble_stack_char_get_test(&log_update_char, &char_len, log_update_value);
    // wrote to update characteristic
    // search for and select the correct log
    printf("requested logname: %s\n", log_update_value);
    for (int i = 0; i < EVENTUAL_REC_SIZE; i++) {
      if(strncmp(saved_records[i].logname, (char*)log_update_value, STORAGE_LOG_LEN) == 0) {
        selected_record = i;
        break;
      }
    }
    if (saved_records[selected_record].length == 0) {
      uint8_t stop = 0x1;
      // clear update value
      memset(log_update_value, 0, STORAGE_LOG_LEN);
      simple_ble_stack_char_set(&log_update_char, STORAGE_LOG_LEN, log_update_value);
      // set stop
      simple_ble_stack_char_set(&log_notify_char, 1, &stop);
      simple_ble_notify_char(&log_notify_char);
      return;
    }
    else if (strncmp(saved_records[selected_record].logname, (const char*) log_update_value, STORAGE_LOG_LEN)) {
      printf("requested logname does not exist!\n");
      uint8_t stop = 0x1;
      // clear update value
      memset(log_update_value, 0, STORAGE_LOG_LEN);
      simple_ble_stack_char_set(&log_update_char, STORAGE_LOG_LEN, log_update_value);
      // set stop
      simple_ble_stack_char_set(&log_notify_char, 1, &stop);
      simple_ble_notify_char(&log_notify_char);
      return;
    }
    printf("selected logname: %s\n", saved_records[selected_record].logname);
    // clear update value
    memset(log_update_value, 0, STORAGE_LOG_LEN);
    simple_ble_stack_char_set(&log_update_char, STORAGE_LOG_LEN, log_update_value);

    size_t index = 0;

    //form sendable packet header
    memcpy(log_buffer, address, ADDRESS_SIZE);
    index += ADDRESS_SIZE;
    memcpy(log_buffer + index, &seq_num, 1);
    index+=1;

    // get next bytes of log
    size_t actual_read = EVENTUAL_BUFFER_SIZE - index;
    int rc = signpost_storage_read(log_buffer + index, &actual_read, &saved_records[selected_record]);
    if (rc != 0) {
      printf("error getting packets from storage\n");
      uint8_t stop = 0x1;
      simple_ble_stack_char_set(&log_notify_char, 1, &stop);
      return;
    }

    // fix up offset of record based on complete messages contained
    size_t search = index;
    while(search < EVENTUAL_BUFFER_SIZE) {
      uint16_t pkt_len = log_buffer[search+1];
      if (pkt_len == 0) break;
      printf("found packet of length %u\n", pkt_len);

      if (search + sizeof(uint16_t) + pkt_len >= EVENTUAL_BUFFER_SIZE) break;
      search += sizeof(uint16_t) + pkt_len;
    }
    if (search == index) {
      //done, so notify disconnect
      printf("disconnect and delete record\n");
      uint8_t stop = 0x1;
      simple_ble_stack_char_set(&log_notify_char, 1, &stop);
      simple_ble_notify_char(&log_notify_char);

      stop = 0x0;
      simple_ble_stack_char_set(&log_notify_char, 1, &stop);
      //delete log
      rc = signpost_storage_delete(&saved_records[selected_record]);
      if (rc != 0) {
        printf("Failed to delete record!\n");
      }
      saved_records[selected_record].length = 0;
      //XXX handle deleted records - reorder storage
      return;
    }

    // search is now the total length of non-fragmented packets
    saved_records[selected_record].offset += search - index;
    // zero out fragmented packets remaining in buffer
    memset(log_buffer+search, 0, EVENTUAL_BUFFER_SIZE-search);

    // write data to nrf and notify that read is available
    rc = simple_ble_stack_char_set(&log_read_char, search, log_buffer);
    if (rc != 0) {
      printf("read update error: %d\n", rc);
    }
    simple_ble_notify_char(&log_notify_char);

    if (rc != 0) {
      printf("notify update error: %d\n", rc);
    }
  }
}

void ble_evt_connected(ble_evt_t* p_ble_evt __attribute__ ((unused))) {
    //this might also need to be here
}

void ble_evt_disconnected(ble_evt_t* p_ble_evt __attribute__ ((unused))) {
    uint8_t stop = 0x0;
    simple_ble_stack_char_set(&log_notify_char, 1, &stop);
}

void ble_evt_user_handler (ble_evt_t* p_ble_evt __attribute__ ((unused))) {
    //and maybe this
}


#define SUCCESS 0
#define FAILURE 1

static void track_failures(bool fail) {
    static int fail_counter = 0;

    if(fail) {
        fail_counter++;
        led_on(2);
        delay_ms(5);
        led_off(2);
    } else {
        fail_counter = 0;
        led_on(3);
        delay_ms(5);
        led_off(3);
    }

    if(fail_counter == 10) {
        //try reseting the xdot and rejoining the network
        printf("20 straight failures. soft reset!\n");
        xdot_reset();
        delay_ms(500);
        lora_state = LORA_ERROR;
    } else if(fail_counter == 40) {
        //hard reset the xdot using the power pin and rejoin the network
        printf("40 straight failures. Hard reset!\n");
        gpio_set(LORA_POWER);
        delay_ms(1000);
        gpio_clear(LORA_POWER);
        delay_ms(1000);

        xdot_reset();
        delay_ms(500);
        lora_state = LORA_ERROR;
    }
}

static int cellular_state = SARA_U260_NO_SERVICE;
static void timer_callback (
    int callback_type __attribute__ ((unused)),
    int length __attribute__ ((unused)),
    int unused __attribute__ ((unused)),
    void * callback_args __attribute__ ((unused))) {

    if(currently_sending) return;
    static uint8_t send_counter = 0;

    if(queue_head != queue_tail) {

        currently_sending = true;

        if(cellular_state == SARA_U260_NO_SERVICE) {
            int ret = sara_u260_check_connection();
            if(ret < SARA_U260_SUCCESS) {
                //we lost service - return;
                printf("Still cellular no service\n");
                cellular_state = SARA_U260_NO_SERVICE;
            } else {
                printf("Regained cellular service\n");
                cellular_state = SARA_U260_SUCCESS;
            }
        }

        if(calc_queue_length() > QUEUE_SIZE*0.66 && cellular_state == SARA_U260_SUCCESS) {
            printf("Attempting to send data with cellular radio\n");
            printf("Queue size is %d\n", calc_queue_length());

            //do we have cell service?
            int ret = sara_u260_check_connection();
            if(ret < SARA_U260_SUCCESS) {
                printf("Can't send - no service\n");
                //we lost service - return;
                cellular_state = SARA_U260_NO_SERVICE;
                currently_sending = false;;
                return;
            }

            //the queue has gotten too long, let's just send it over cellular
            //make a big buffer to pack a large chunk of the queue into
            uint8_t cell_buffer[CELL_POST_SIZE];
            size_t used = 0;
            memcpy(cell_buffer, address, ADDRESS_SIZE);
            used += ADDRESS_SIZE;
            memcpy(cell_buffer + used, &seq_num, 1);
            used += 1;
            bool done = false;
            uint8_t temp_head = queue_head;
            while(!done) {
                if(used + 2 + data_length[queue_head] < CELL_POST_SIZE) {
                    cell_buffer[used] = (uint8_t)((data_length[temp_head] & 0xff00) >> 8);
                    cell_buffer[used+1] = (uint8_t)((data_length[temp_head] & 0xff));
                    used += 2;
                    memcpy(cell_buffer + used, data_queue[temp_head], data_length[temp_head]);
                    used += data_length[temp_head];
                    count_module_packet(data_address[temp_head]);
                    increment_queue_pointer(&temp_head);

                    if(temp_head == queue_tail) {
                        done = true;
                    }
                } else {
                    done = true;
                }
            }

            printf("Attempting to perform post on network\n");

            //now that we have the buffer, perform the http post
            ret = sara_u260_basic_http_post(POST_ADDRESS, "/signpost", cell_buffer, used);
            if(ret < SARA_U260_SUCCESS) {
                printf("SARA U260 Post Failed\n");
            } else {
                uint8_t rbuf[20];
                ret = sara_u260_get_post_response(rbuf,20);
                if(ret >= SARA_U260_SUCCESS) {
                    if(!strncmp((char*)(&rbuf[9]),"200 OK", 6)) {
                        seq_num++;
                        printf("SARA U260 Post Successful\n");
                        queue_head = temp_head;
                    } else {
                        printf("SARA U260 Post Failed - Bad Response of %.*s\n",6,&(rbuf[9]));
                    }
                } else {
                    printf("SARA U260 Post Failed - No Response\n");
                }

            }
        } else if (lora_state == LORA_JOINED) {
            //send another packet over LORA
            uint8_t LoRa_send_buffer[(ADDRESS_SIZE + BUFFER_SIZE + 1)];
            //count the packet
            count_module_packet(data_address[queue_head]);

            //send the packet
            memcpy(LoRa_send_buffer, address, ADDRESS_SIZE);
            memcpy(LoRa_send_buffer+ADDRESS_SIZE, &seq_num, 1);
            memcpy(LoRa_send_buffer+ADDRESS_SIZE + 1, data_queue[queue_head], BUFFER_SIZE);

            xdot_wake();
            int status = xdot_send(LoRa_send_buffer,data_length[queue_head]+ADDRESS_SIZE+1);

            //parse the HCI layer error codes
            if(status < 0) {
                printf("Xdot send failed\n");
                track_failures(FAILURE);
            } else {
                track_failures(SUCCESS);
                printf("Xdot send succeeded!\n");
                seq_num++;
                increment_queue_pointer(&queue_head);
            }

            xdot_sleep();
        }
        currently_sending = false;
    }


    send_counter++;

    //every minute put a status packet on the queue
    //also send an energy report to the controller
    if(send_counter == 5) {
        //increment the sequence number
        status_send_buf[status_data_offset] = number_of_modules;

        //make an array of energy reports based on the number_of_modules
        signpost_energy_report_t energy_report;
        signpost_energy_report_module_t* reps = malloc(number_of_modules*sizeof(signpost_energy_report_module_t));
        if(!reps) return;

        //copy the modules and their send numbers into the buffer
        //at the same time total up the packets sent
        uint8_t i = 0;
        uint16_t packets_total = 0;
        for(; i < NUMBER_OF_MODULES; i++){
            if(module_num_map[i] != 0) {
                status_send_buf[status_data_offset+1+ i*2] = module_num_map[i];
                reps[i].application_id = module_num_map[i];
                status_send_buf[status_data_offset+1+ i*2 + 1] = module_packet_count[i];
                packets_total += module_packet_count[i];
            } else {
                break;
            }
        }

        // copy logname lengths, lognames, and log lengths into buffer
        // | log remaining length (uint16_t) | log name length (uint8_t) | log name (up to 32 uint8_t) |
        size_t eventual_status_index = status_data_offset + 1 + number_of_modules*2;
        status_send_buf[eventual_status_index] = num_saved_records;
        eventual_status_index += 1;
        i = 0;
        for (; i < num_saved_records; i++){
          size_t logname_len = strnlen(saved_records[i].logname, STORAGE_LOG_LEN);
          if (logname_len == STORAGE_LOG_LEN) {
            printf("Bad logname found when trying to send status\n");
          }
          //printf("saved record offset %d\n", saved_records[i].offset);
          //printf("saved record length %d\n", saved_records[i].length);
          uint16_t remaining = (saved_records[i].length - saved_records[i].offset)/1024 + 1;
          if (saved_records[i].length == 0) remaining = 0;
          status_send_buf[eventual_status_index] = (uint8_t) ((remaining & 0xff00) >> 8);
          status_send_buf[eventual_status_index+1] = (uint8_t) (remaining & 0xff);
          eventual_status_index += 2;
          status_send_buf[eventual_status_index] = logname_len & 0xff;
          eventual_status_index += 1;
          memcpy(status_send_buf + eventual_status_index, saved_records[i].logname, logname_len);
          eventual_status_index += logname_len;
        }

        printf("Sending energy query\n");
        signpost_energy_information_t info;
        int energy_failed = signpost_energy_query(&info);
        if(energy_failed < 0) printf("ERROR: Energy query failed\n");

        if(energy_failed >= 0) {
            printf("Used %lu uWh since last reset\n",info.energy_used_since_reset_uWh);
            //now figure out the percentages for each module
            for(i = 0; i < NUMBER_OF_MODULES; i++){
                if(module_num_map[i] != 0) {
                    reps[i].energy_used_uWh =
                            (uint32_t)((module_packet_count[i]/(float)packets_total)*info.energy_used_since_reset_uWh);
                    printf("Module %d used %lu uWh since last reset\n",module_num_map[i],reps[i].energy_used_uWh);
                } else {
                    break;
                }
            }

            //now pack it into an energy report structure
            energy_report.num_reports = number_of_modules;
            energy_report.reports = reps;

            //send it to the controller
            printf("Sending energy report\n");
            int rc = signpost_energy_report(&energy_report);
            if(rc < 0) {
                printf("Energy report failed\n");
            } else {
                rc = signpost_energy_reset();
                if(rc < 0) printf("ERROR: Energy reset failed\n");

                //reset_packet_send_bufs
                for(i = 0; i < NUMBER_OF_MODULES; i++) {
                    module_packet_count[i] = 0;
                }
            }
        }


        //calculate and add the queue size in the status packet
        if(queue_tail >= queue_head) {
            status_send_buf[eventual_status_index] = queue_tail-queue_head;
        } else {
            status_send_buf[eventual_status_index] = QUEUE_SIZE-(queue_head-queue_tail);
        }
        eventual_status_index += 1;

        uint8_t status_len = 1 + eventual_status_index - status_data_offset;//2+number_of_modules*2+1;
        status_send_buf[status_length_offset] = status_len;

        //put it in the send buffer
        add_buffer_to_queue(0x22, status_send_buf, eventual_status_index);

        //reset send_counter
        send_counter = 0;

        free(reps);
    }

    app_watchdog_tickle_kernel();
}

#ifndef APP_KEY
#error Missing required define APP_KEY of format: 0x00, 0x00,... (x32)
#endif
static uint8_t appKey[16] = { APP_KEY };

static int join_lora_network(void) {
    xdot_wake();
    delay_ms(1000);

    xdot_init();

    //setup lora
    uint8_t appEUI[8] = {0};

    int rc = xdot_set_ack(1);
    rc |= xdot_set_txpwr(20);
    rc |= xdot_set_txdr(2);
    rc |= xdot_set_adr(0);
    if(rc < 0)  printf("XDot settings error!\n");

    do {
        printf("Joining Network...\n");
        rc = xdot_join_network(appEUI, appKey);
        if(rc < 0) {
            printf("Failed to join network\n");
            track_failures(FAILURE);
            delay_ms(5000);
            xdot_wake();
        }
        app_watchdog_tickle_kernel();
    } while (rc < 0);

    xdot_sleep();
    track_failures(SUCCESS);
    lora_state = LORA_JOINED;
    printf("Joined successfully! Starting packets\n");

    return 0;
}

int main (void) {
    printf("starting app!\n");
    //do module initialization
    //I did it last because as soon as we init we will start getting callbacks
    //those callbacks depend on the setup above
    int rc;


    static api_handler_t networking_handler = {NetworkingApiType, networking_api_callback};
    static api_handler_t update_handler = {UpdateApiType, update_api_callback};
    static api_handler_t* handlers[] = {&networking_handler,&update_handler,NULL};
    do {
        rc = signpost_initialization_module_init(ModuleAddressRadio,handlers);
        if (rc<0) {
            delay_ms(5000);
        }
    } while (rc<0);

    // eventual send data
    // read existing log info
    printf("Found the following existing files:\n");
    num_saved_records = EVENTUAL_REC_SIZE;
    while (signpost_storage_scan(saved_records, &num_saved_records) < 0) {
      delay_ms(500);
    }
    for(size_t i = 0; i < num_saved_records; i++) {
      if (saved_records[i].length == 0) {
        num_saved_records = i;
        break;
      }
      printf("  %s\n", saved_records[i].logname);
    }
    printf("\n");

    gpio_enable_output(BLE_RESET);
    gpio_clear(BLE_RESET);
    delay_ms(100);
    gpio_set(BLE_RESET);

    gpio_enable_output(LORA_POWER);
//    gpio_enable_output(GSM_POWER);
    gpio_set(LORA_POWER);
 //   gpio_set(GSM_POWER);
    delay_ms(500);
    gpio_clear(LORA_POWER);
  //  gpio_clear(GSM_POWER);

    delay_ms(1000);
    rc = sara_u260_init();

    status_send_buf[0] = strlen("lab11/radio-status");
    printf("%02x\n", status_send_buf[0]);
    memcpy(status_send_buf+1,"lab11/radio-status",strlen("lab11/radio-status"));
    for(int k = 0; k < status_send_buf[0] + 1; k++) {
      printf("%02x", status_send_buf[k]);
    }
    printf("\n");
    status_length_offset = 1 + strlen("lab11/radio-status");
    status_data_offset = status_length_offset+1;
    status_send_buf[status_data_offset] = 0x02;
    status_data_offset++;

    //ble
    conn_handle = simple_ble_init(&ble_config)->conn_handle;
    simple_adv_only_name();

    // setup watchdog
    app_watchdog_set_kernel_timeout(60000);
    app_watchdog_start();

    // setup timer
    static tock_timer_t timer;
    timer_every(2000, timer_callback, NULL, &timer);

    while(true) {
        if(lora_state != LORA_JOINED) {
            join_lora_network();
        }
        yield();
    }
}
