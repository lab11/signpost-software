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
static simple_ble_service_t signpost_service = {
    .uuid128 = {{0x5C, 0xC2, 0x0D, 0x14, 0x6D, 0x28, 0x49, 0x7A,
                 0x8F, 0x56, 0x66, 0xB7, 0x6F, 0x00, 0xE9, 0x75}}
};
static simple_ble_char_t log_update_char = {.uuid16 = 0x6F01};
static simple_ble_char_t log_read_char = {.uuid16 = 0x6F02};
static simple_ble_char_t log_notify_char = {.uuid16 = 0x6F03};

#define UPDATE_VAL_LEN 4
#define LOG_BUFFER_LEN 128
static uint8_t log_update_value [UPDATE_VAL_LEN];
static uint8_t log_buffer[LOG_BUFFER_LEN];
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

// for now, just use queue size
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

/*static int add_message_to_buffer(uint8_t *buffer, size_t buf_len, size_t* offset, uint8_t *data, size_t len) {
  if (len + 2 > buf_len) return TOCK_FAIL;
  // lengths are only 1 byte, but header takes 2 bytes
  buffer[*offset] = 0;
  buffer[*offset+1] = (uint8_t) (len & 0xff);
  *offset += 2;

  memcpy(buffer+*offset, data, len);
  *offset += len;

  return TOCK_SUCCESS;
}*/

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
        if(message_type == NetworkingPublishMessage) {
            int rc = add_buffer_to_queue(source_address, message, message_length);
            signpost_networking_publish_reply(source_address, rc);
        }
    }
}

__attribute__ ((const))
void services_init(void) {
    simple_ble_add_service(&signpost_service);

    simple_ble_add_stack_characteristic(1, 1, 0, 0,
        UPDATE_VAL_LEN, (uint8_t*) log_update_value,
        &signpost_service, &log_update_char);
    simple_ble_add_stack_characteristic(1, 0, 0, 0,
        LOG_BUFFER_LEN, (uint8_t*) log_buffer,
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

/*static uint32_t simple_ble_stack_char_get_test (simple_ble_char_t* char_handle, uint16_t* len, uint8_t* buf) {
    ble_gatts_value_t value = {
        .len = *len,
        .offset = 0,
        .p_value = buf,
    };

    return sd_ble_gatts_value_get(conn_handle, char_handle->char_handle.value_handle, &value);
}*/

void ble_evt_write(ble_evt_t* p_ble_evt) {
  static bool added_queue = false;

  if(simple_ble_is_char_event(p_ble_evt, &log_update_char)) {

    //This write is like an Ack for reading the previous packet
    if(added_queue) {
        printf("BLE send succeeded!\n");
        seq_num++;
        increment_queue_pointer(&queue_head);
    }
        //a write to the update char acks that ble received the log data
    if (queue_head == queue_tail) {
        //No these is not another value
        //Send a stop notify
        uint8_t stop = 0x1;
        // clear update value
        memset(log_update_value, 0, UPDATE_VAL_LEN);
        simple_ble_stack_char_set(&log_update_char, UPDATE_VAL_LEN, log_update_value);
        // set stop
        simple_ble_stack_char_set(&log_notify_char, 1, &stop);
        simple_ble_notify_char(&log_notify_char);
        added_queue = false;
    } else {
        //There is another value - form a packet and notify

        //form sendable packet header
        size_t index = 0;
        memcpy(log_buffer, address, ADDRESS_SIZE);
        index += ADDRESS_SIZE;
        memcpy(log_buffer + index, &seq_num, 1);
        index+=1;
        log_buffer[index] = (uint8_t)((data_length[queue_head] & 0xff00) >> 8);
        log_buffer[index+1] = (uint8_t)((data_length[queue_head] & 0xff));
        index += 2;
        memcpy(log_buffer + index, data_queue[queue_head], data_length[queue_head]);
        index += data_length[queue_head];

        //count the packet
        count_module_packet(data_address[queue_head]);

        // write data to nrf and notify that read is available
        int rc = simple_ble_stack_char_set(&log_read_char, index, log_buffer);
        if (rc != 0) {
          printf("read update error: %d\n", rc);
        }
        simple_ble_notify_char(&log_read_char);

        added_queue = true;
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
    if(send_counter == 30) {
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
            status_send_buf[status_data_offset+1+number_of_modules*2] = queue_tail-queue_head;
        } else {
            status_send_buf[status_data_offset+1+number_of_modules*2] = QUEUE_SIZE-(queue_head-queue_tail);
        }

        uint8_t status_len = 2+number_of_modules*2+1;
        status_send_buf[status_length_offset] = status_len;

        //put it in the send buffer
        add_buffer_to_queue(0x22, status_send_buf, status_data_offset+1+number_of_modules*2+1);

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
    static api_handler_t* handlers[] = {&networking_handler,NULL};
    do {
        rc = signpost_initialization_module_init("radio",ModuleAddressRadio,handlers);
        if (rc<0) {
            delay_ms(5000);
        }
    } while (rc<0);

    gpio_enable_output(BLE_RESET);
    gpio_clear(BLE_RESET);
    delay_ms(100);
    gpio_set(BLE_RESET);

    gpio_enable_output(LORA_POWER);
    gpio_set(LORA_POWER);
    delay_ms(500);
    gpio_clear(LORA_POWER);

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
