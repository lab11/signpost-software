#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "crc.h"
#include "strnstr.h"

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

#define POST_ADDRESS "ec2-35-166-179-172.us-west-2.compute.amazonaws.com"

//definitions for the i2c
#define BUFFER_SIZE 128
#define ADDRESS_SIZE 6
#define NUMBER_OF_MODULES 8
#define CELL_POST_SIZE 2000

//array of the data we're going to send on the radios
//make a queue of 30 deep
#define QUEUE_SIZE 30
#define DOWNLINK_QUEUE_SIZE 10
uint8_t data_queue[QUEUE_SIZE][BUFFER_SIZE];
uint8_t data_length[QUEUE_SIZE];
uint8_t data_address[QUEUE_SIZE];
uint8_t queue_head = 0;
uint8_t queue_tail = 0;
uint32_t lora_packets_sent = 1;
uint8_t module_num_map[NUMBER_OF_MODULES] = {0};
uint8_t number_of_modules = 0;
uint8_t module_packet_count[NUMBER_OF_MODULES] = {0};
uint8_t status_send_buf[50] = {0};
uint8_t status_length_offset = 0;
uint8_t status_data_offset = 0;
static uint8_t sn = 0;
static bool currently_sending = false;

#ifndef COMPILE_TIME_ADDRESS
#error Missing required define COMPILE_TIME_ADDRESS of format: 0xC0, 0x98, 0xE5, 0x12, 0x00, 0x00
#endif
static uint8_t address[ADDRESS_SIZE] = { COMPILE_TIME_ADDRESS };


#define LORA_JOINED 0
#define LORA_ERROR -1
int lora_state = LORA_ERROR;

//extern module state
extern module_state_t module_info;

static void increment_queue_pointer(uint8_t* p) {
    if(*p == (QUEUE_SIZE -1)) {
        *p = 0;
    } else {
        (*p)++;
    }
}

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

static int lookup_module_num(char* mod_name) {
    //short circuit the controller
    //everything else needs to send a packet first
    if(!strncmp(mod_name,"signpost/control", NAME_LEN)) {
        return ModuleAddressController;
    } else {
        return TOCK_FAIL;
    }

    //currently only downlinking to the controller
    //downlink to all modules coming soon
    /*else {
        for(uint8_t i = 0; i < NUM_MODULES; i++) {
            if(!strncmp(module_info.names[i],mod_name,NAME_LEN)) {
                return module_info.i2c_address_mods[i];
            }
        }
        return TOCK_FAIL;
    }*/
}

static int8_t send_downlink_message(uint8_t* buffer, uint8_t len) {
    //extract the module name
    uint8_t slen = buffer[0];
    uint8_t dlen = buffer[slen+1];

    if(slen+ dlen + 2 > len) {
        return TOCK_FAIL;
    }

    char mod_name[NAME_LEN+1] = {0};
    char topic[NAME_LEN+1] = {0};
    uint8_t* find1 = memchr(buffer+1,'/',slen);
    uint8_t* find2 = memchr(find1+1,'/',slen-(find1+1-(buffer+1)));
    memcpy(mod_name,buffer+1,find2-(buffer+1));
    memcpy(topic,find2+1,slen-(find2-(buffer+1))-1);
    printf("Downlink for %s at topic %s\n",mod_name,topic);

    //Do we know the module name?
    int ret = lookup_module_num(mod_name);
    if(ret == TOCK_FAIL) {
        return TOCK_FAIL;
    } else {
        //okay we have the destination - dispatch the message
        return signpost_networking_subscribe_send(ret, topic, &buffer[slen+2], dlen);
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
        if(message_type == NetworkingPublishMessage) {
            int rc = add_buffer_to_queue(source_address, message, message_length);
            signpost_networking_publish_reply(source_address, rc);
        }
    }
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

            //the queue has gotten to long, let's just send it over cellular
            //make a big buffer to pack a large chunk of the queue into
            uint8_t cell_buffer[CELL_POST_SIZE];
            size_t used = 0;
            memcpy(cell_buffer, address, ADDRESS_SIZE);
            used += ADDRESS_SIZE;
            memcpy(cell_buffer + used, &sn, 1);
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
                        sn++;
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
            memcpy(LoRa_send_buffer+ADDRESS_SIZE, &sn, 1);
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
                sn++;
                increment_queue_pointer(&queue_head);

                //See if the XDOT got any data in the response
                uint8_t rbuf[128];
                status = xdot_receive(rbuf, 128);
                if(status > 0) {
                    printf("Xdot received %d bytes of data\n",status);
                    int rc = send_downlink_message(rbuf, status);
                    if(rc < 0) {
                        printf("Downlink failed!\n");
                    } else {
                        printf("Downlink success!\n");
                    }
                } else if(status == 0) {
                    printf("Xdot received no data!\n");
                } else {
                    printf("Xdot receive failed!\n");
                }
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
        rc = signpost_initialization_module_init("signpost","radio",ModuleAddressRadio,handlers);
        if (rc<0) {
            delay_ms(5000);
        }
    } while (rc<0);

    //turn off ble
    gpio_enable_output(BLE_POWER);
    gpio_set(BLE_POWER);

    gpio_enable_output(LORA_POWER);
    gpio_set(LORA_POWER);
    delay_ms(500);
    gpio_clear(LORA_POWER);

    delay_ms(1000);
    rc = sara_u260_init();

    status_send_buf[0] = strlen("signpost/radio/status");
    memcpy(status_send_buf+1,"signpost/radio/status",strlen("signpost/radio/status"));
    status_length_offset = 1 + strlen("signpost/radio/status");
    status_data_offset = status_length_offset+1;
    status_send_buf[status_data_offset] = 0x01;
    status_data_offset++;

    //enable watchdog
    app_watchdog_set_kernel_timeout(60000);
    app_watchdog_start();

    //setup timer
    static tock_timer_t timer;
    timer_every(2000, timer_callback, NULL, &timer);

    while(true) {
        if(lora_state != LORA_JOINED) {
            join_lora_network();
        }
        yield();
    }
}
