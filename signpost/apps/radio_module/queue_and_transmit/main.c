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


#define UMICH_COMPANY_IDENTIFIER 0x02E0

/*static simple_ble_config_t ble_config = {
    .platform_id        = 0x00,
    .device_id          = DEVICE_ID_DEFAULT,
    .adv_name           = (char *)DEVICE_NAME,
    .adv_interval       = MSEC_TO_UNITS(300, UNIT_0_625_MS),
    .min_conn_interval  = MSEC_TO_UNITS(500, UNIT_1_25_MS),
    .max_conn_interval  = MSEC_TO_UNITS(1250, UNIT_1_25_MS),
};*/

//definitions for the i2c
#define BUFFER_SIZE 50
#define ADDRESS_SIZE 6
#define NUMBER_OF_MODULES 8

//array of the data we're going to send on the radios
//make a queue of 30 deep
#define QUEUE_SIZE 30
uint8_t data_queue[QUEUE_SIZE][BUFFER_SIZE];
uint8_t data_length[QUEUE_SIZE];
uint8_t queue_head = 0;
uint8_t queue_tail = 0;
uint32_t lora_packets_sent = 1;
uint8_t module_num_map[NUMBER_OF_MODULES] = {0};
uint8_t number_of_modules = 0;
uint8_t module_packet_count[NUMBER_OF_MODULES] = {0};
uint8_t status_send_buf[50] = {0};

//these structures for reporting energy to the controller


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
        return -1;
    } else {
        data_queue[queue_tail][0] = addr;
        if(len <= BUFFER_SIZE -1) {
            memcpy(data_queue[queue_tail]+1, buffer, len);
            data_length[queue_tail] = len+1;
        } else {
            memcpy(data_queue[queue_tail]+1, buffer, BUFFER_SIZE-1);
            data_length[queue_tail] = BUFFER_SIZE;
        }
        increment_queue_pointer(&queue_tail);
        return 0;
    }
}

static void networking_api_callback(uint8_t source_address,
        signbus_frame_type_t frame_type, __attribute ((unused)) signbus_api_type_t api_type,
        uint8_t message_type, size_t message_length, uint8_t* message) {

    if (frame_type == NotificationFrame || frame_type == CommandFrame) {
        if(message_type == NetworkingSend) {
            add_buffer_to_queue(source_address, message, message_length);
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
        int ret = sara_u260_get_get_partial_response((uint8_t*)read_buf, offset, 200);
        if(ret < SARA_U260_SUCCESS) {
            return ret;
        }

        //find if the end string is in this string
        char* pt = strnstr(read_buf,"\r\n\r\n",200);
        if(pt != NULL) {
            offset += (pt-read_buf);
            header_done = true;
            break;
        }

        //make sure we didn't just get part of it
        if(!strncmp(read_buf+199,"\r",1) || !strncmp(read_buf+198,"\r\n",2) || !strncmp(read_buf+197,"\r\n\r",3)) {
            //we could have gotten part of it, shift a bit and read again
            offset += 4;
        } else {
            offset += 200;
        }

        if(ret < 200 && header_done == false) {
            //we got to the end and didn't find the end of the header
            return SARA_U260_ERROR;
        }
    }

    return offset;
}

static void update_api_callback(uint8_t source_address,
        signbus_frame_type_t frame_type, __attribute ((unused)) signbus_api_type_t api_type,
        uint8_t message_type, size_t message_length, uint8_t* message) {

    static bool got_transfer = false;

    if(frame_type == CommandFrame) {
        if(message_type == UpdateRequestMessage) {
            //parse the request
            uint32_t url_len;
            uint32_t version_len;
            char* url;
            char* version;
            if(message_length >= 4) {
                memcpy(&url_len, message, 4);
            } else {
                return;
            }

            if(message_length >= 8 + url_len) {
                memcpy(&version_len, message+4+url_len, 4);
            } else {
                return;
            }

            if(message_length >= 8 + url_len + version_len) {
                url = malloc(url_len);
                if(!url) return;

                version = malloc(version_len);
                if(!version) {
                    free(url);
                    return;
                }

                memcpy(url, message+4, url_len);
                memcpy(version, message+4+url_len, version_len);
            } else {
                return;
            }

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
                return;
            }

            char* post_slash_version = malloc(url_len - slash+10);
            if(!post_slash_version) {
                free(url);
                free(version);
                free(pre_slash);
                return;
            }

            char* post_slash_app = malloc(url_len - slash+10);
            if(!post_slash_app) {
                free(url);
                free(version);
                free(pre_slash);
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

            //send the http get for the version file
            int ret = sara_u260_basic_http_get(pre_slash,post_slash_version);
            free(post_slash_version);

            if(ret != SARA_U260_SUCCESS) {
                free(version);
                free(pre_slash);
                free(post_slash_app);
                return;
            }

            int offset = find_end_of_response_header();
            if(offset < SARA_U260_SUCCESS) {
                free(version);
                free(pre_slash);
                free(post_slash_app);
                return;
            }

            //read the body of the file
            char read_buf[200];
            ret = sara_u260_get_get_partial_response((uint8_t*)read_buf, offset, 200);
            if(ret == 200) {
                //the info file should not be this long
                free(version);
                free(pre_slash);
                free(post_slash_app);
                return;
            }

            //parse the body of the file
            char* version_line = strnstr(read_buf,"\n",ret);
            if(!version_line) {
                free(version);
                free(pre_slash);
                free(post_slash_app);
                return;
            }

            char* len_line = strnstr(version_line,"\n",ret - (version_line-read_buf));
            if(!len_line) {
                free(version);
                free(pre_slash);
                free(post_slash_app);
                return;
            }

            char* crc_line = strnstr(len_line,"\n",ret - (len_line-read_buf));
            if(!len_line) {
                free(version);
                free(pre_slash);
                free(post_slash_app);
                return;
            }

            //compare the version
            int cmp = strncmp(read_buf, version, (version_line - read_buf));
            free(version);
            if(cmp <= 0) {
                //don't update because versions match
                free(pre_slash);
                free(post_slash_app);
                return;
            }

            //extract the length and the CRC
            uint16_t crc = strtol(len_line,&crc_line,16);
            uint32_t len = strtol(version_line,&len_line,16);


            //okay now fetch the actual update
            ret = sara_u260_basic_http_get(pre_slash,post_slash_app);
            free(pre_slash);
            free(post_slash_app);
            if(ret != SARA_U260_SUCCESS) {
                return;
            }

            //find the end of the response
            offset = find_end_of_response_header();
            if(offset < SARA_U260_SUCCESS) {
                return;
            }

            //now stream the binary back in chunks
            bool done_transferring = false;
            offset = 0;
            while(!done_transferring) {
                ret = sara_u260_get_get_partial_response((uint8_t*)read_buf, offset, 200);
                if(ret < SARA_U260_SUCCESS) {
                    return;
                } else if (ret < 200) {
                    //done getting responses
                    signpost_update_transfer_reply(source_address,(uint8_t*)read_buf,offset,ret);
                    done_transferring = true;
                } else {
                    signpost_update_transfer_reply(source_address,(uint8_t*)read_buf,offset,200);
                    offset += 200;
                }

                ret = yield_for_with_timeout(&got_transfer, 10000);
                if(ret  < TOCK_SUCCESS) return;
            }

            signpost_update_done_reply(source_address,UpdateFetched,len,crc);
        }
    } else if(frame_type == ResponseFrame) {
        if(message_type == UpdateResponseMessage) {
            got_transfer = true;
        }
    } else {
        return;
    }
}

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

void ble_evt_connected(ble_evt_t* p_ble_evt __attribute__ ((unused))) {
    //this might also need to be here
}

void ble_evt_disconnected(ble_evt_t* p_ble_evt __attribute__ ((unused))) {
    //this too
}

void ble_evt_user_handler (ble_evt_t* p_ble_evt __attribute__ ((unused))) {
    //and maybe this
}

static uint8_t sn = 0;
static bool currently_sending = false;


#define SUCCESS 0
#define FAILURE 1

static int track_failures(bool fail) {
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

    if(fail_counter == 20) {
        //try reseting the xdot and rejoining the network
        printf("20 straight failures. soft reset!\n");
        xdot_reset();
        delay_ms(500);

        return join_lora_network();
    } else if(fail_counter == 40) {
        //hard reset the xdot using the power pin and rejoin the network
        printf("40 straight failures. Hard reset!\n");
        gpio_set(LORA_POWER);
        delay_ms(1000);
        gpio_clear(LORA_POWER);
        delay_ms(1000);

        xdot_reset();
        delay_ms(500);

        return join_lora_network();

    } else if(fail_counter == 50) {
        //just ask the controller to reset us and see if that fixes it
        int rc;
        do {
            rc = signpost_energy_duty_cycle(1000);
            delay_ms(1000);
        } while (rc < 0);
    }

    return -1;
}

static void timer_callback (
    int callback_type __attribute__ ((unused)),
    int length __attribute__ ((unused)),
    int unused __attribute__ ((unused)),
    void * callback_args __attribute__ ((unused))) {

    if(currently_sending) return;

    static uint8_t LoRa_send_buffer[(ADDRESS_SIZE + BUFFER_SIZE)];
    static uint8_t send_counter = 0;

    if(queue_head != queue_tail) {

        //count the packet
        count_module_packet(data_queue[queue_head][0]);

        data_queue[queue_head][2] = sn;

        //send the packet
        memcpy(LoRa_send_buffer, address, ADDRESS_SIZE);
        memcpy(LoRa_send_buffer+ADDRESS_SIZE, data_queue[queue_head], BUFFER_SIZE);

        currently_sending = true;
        xdot_wake();
        int status = xdot_send(LoRa_send_buffer,data_length[queue_head]+ADDRESS_SIZE);

        //parse the HCI layer error codes
        if(status < 0) {
            printf("Xdot send failed\n");
            track_failures(FAILURE);
        } else {
            track_failures(SUCCESS);
            printf("Xdot send succeeded!\n");
            sn++;
            increment_queue_pointer(&queue_head);
        }
    }

    currently_sending = false;
    xdot_sleep();

    send_counter++;

    //every minute put a status packet on the queue
    //also send an energy report to the controller
    if(send_counter == 30) {
        //increment the sequence number
        status_send_buf[1]++;
        status_send_buf[2] = number_of_modules;

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
                status_send_buf[3+ i*2] = module_num_map[i];
                reps[i].application_id = module_num_map[i];
                status_send_buf[3+ i*2 + 1] = module_packet_count[i];
                packets_total += module_packet_count[i];
            } else {
                break;
            }
        }

        printf("Sending energy query\n");
        signpost_energy_information_t info;
        int rc = signpost_energy_query(&info);
        if(rc < 0) printf("ERROR: Energy query failed\n");

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
        rc = signpost_energy_report(&energy_report);
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

        //calculate and add the queue size in the status packet
        if(queue_tail >= queue_head) {
            status_send_buf[3+number_of_modules*2] = queue_tail-queue_head;
        } else {
            status_send_buf[3+number_of_modules*2] = QUEUE_SIZE-(queue_head-queue_tail);
        }

        //put it in the send buffer
        add_buffer_to_queue(0x22, status_send_buf, 3+number_of_modules*2+1);

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

    int rc = xdot_init();
    if(rc < 0) printf("xDot Init Error!\n");

    //setup lora
    uint8_t appEUI[8] = {0};

    rc  = xdot_set_ack(1);
    rc |= xdot_set_txpwr(20);
    rc |= xdot_set_txdr(3);
    rc |= xdot_set_adr(0);
    if(rc < 0)  printf("XDot settings error!\n");

    do {
        printf("Joining Network...\n");
        rc = xdot_join_network(appEUI, appKey);
        if(rc < 0) {
            printf("Failed to join network\n");
            rc = track_failures(FAILURE);
            delay_ms(5000);
        }
        app_watchdog_tickle_kernel();
    } while (rc < 0);

    xdot_sleep();
    track_failures(SUCCESS);
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

    gpio_enable_output(BLE_POWER);
    gpio_set(BLE_POWER);
    delay_ms(10);
    gpio_clear(BLE_POWER);

    gpio_enable_output(LORA_POWER);
    gpio_set(LORA_POWER);
    delay_ms(500);
    gpio_clear(LORA_POWER);

    status_send_buf[0] = 0x01;
    status_send_buf[1] = 0;
    //ble
    //simple_ble_init(&ble_config);

    //setup a tock timer to
    //eddystone_adv((char *)PHYSWEB_URL,NULL);
    //
    app_watchdog_set_kernel_timeout(60000);
    app_watchdog_start();

    join_lora_network();

    for(uint8_t i = 0; i < QUEUE_SIZE; i++) {
        for(uint8_t j = 0; j < BUFFER_SIZE; j++) {
            data_queue[i][j] = 0;
        }
    }

        //setup timer
    static tock_timer_t timer;
    timer_every(2000, timer_callback, NULL, &timer);
}
