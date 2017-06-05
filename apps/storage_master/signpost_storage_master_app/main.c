#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <i2c_master_slave.h>
#include <gpio.h>
#include <led.h>
#include <sdcard.h>
#include <timer.h>
#include <tock.h>

#include "app_watchdog.h"
#include "signpost_api.h"
#include "signpost_energy.h"
#include "signpost_storage.h"
#include "storage_master.h"

#define DEBUG_RED_LED 0
#define RPC_PENDING_PIN 5

//This is the RPC queue
//For right now we are limiting RPC to 255 characters - if they are over that, too bad
#define MAX_RPC_SIZE 255
#define RPC_QUEUE_SIZE 4
uint8_t rpc_queue[RPC_QUEUE_SIZE][MAX_RPC_SIZE] = {0};
uint8_t rpc_queue_head = 0;
uint8_t rpc_queue_tail = 0;
//invariant: tail +1 (wrapped) !> head
static uint8_t increment_head(void) {
    uint8_t t_head = rpc_queue_head;
    t_head++;
    if(t_head == RPC_QUEUE_SIZE) {
        t_head = 0;
    }
    return t_head;
}

static uint8_t increment_tail(void) {
    uint8_t t_head = rpc_queue_tail;
    t_head++;
    if(t_head == RPC_QUEUE_SIZE) {
        t_head = 0;
    }
    return t_head;
}

static void storage_api_callback(uint8_t source_address,
    signbus_frame_type_t frame_type, signbus_api_type_t api_type,
    uint8_t message_type, size_t message_length, uint8_t* message) {
  int err = SUCCESS;

  if (api_type != StorageApiType) {
    signpost_api_error_reply_repeating(source_address, api_type, message_type, true, true, 1);
    return;
  }

  if (frame_type == NotificationFrame) {
    // XXX unexpected, drop
  } else if (frame_type == CommandFrame) {
    printf("Got a command message!: len = %d\n", message_length);
    for (size_t i=0; i<message_length; i++) {
      printf("%X ", message[i]);
    }
    printf("\n");

    //XXX do some checking that the message type is right and all that jazz
    //XXX also figure out what module index this is, somehow
    int module_index = 0;

    printf("Writing data\n");

    // get initial record
    Storage_Record_Pointer_t write_record = {0};
    write_record.block = storage_status.status_records[module_index].curr.block;
    write_record.offset = storage_status.status_records[module_index].curr.offset;

    // write data to storage
    err = storage_write_record(write_record, message, message_length, &write_record);
    if (err < SUCCESS) {
      printf("Writing error: %d\n", err);
      //XXX: send error
    }

    // update record
    storage_status.status_records[module_index].curr.block = write_record.block;
    storage_status.status_records[module_index].curr.offset = write_record.offset;
    err = storage_update_status();
    if (err < SUCCESS) {
      printf("Updating status error: %d\n", err);
      //XXX: send error
    }
    printf("Complete. Final block: %lu offset: %lu\n", write_record.block, write_record.offset);

    // send response
    err = signpost_storage_write_reply(source_address, (uint8_t*)&write_record);
    if (err < SUCCESS) {
      //XXX: I guess just try to send an error...
    }

  } else if (frame_type == ResponseFrame) {
    // XXX unexpected, drop
  } else if (frame_type == ErrorFrame) {
    // XXX unexpected, drop
  }
}

static void processing_api_callback(uint8_t source_address,
    signbus_frame_type_t frame_type, __attribute__ ((unused)) signbus_api_type_t api_type,
    uint8_t message_type, size_t message_length, uint8_t* message) {

    if(frame_type == NotificationFrame) {
        if(message_type == ProcessingEdisonReadMessage) {
            //prep the read buffer with the RPC at the top of the queue
            //remove the rpc from the queue?
        } else if (message_type == ProcessingEdisonResponseMessage) {
            //send a reply back to the module that the process has started
            if(message_length >= 2) {
                signpost_processing_reply(message[0], ProcessingOneWayMessage, message[1]);
            } else {
                //There is nothing we can do with this
            }
        } else {
            //unexpected - drop
        }
    } else if(frame_type == CommandFrame) {
        if(message_type == ProcessingInitMessage) {
            //not currently supported
        } else if (message_type == ProcessingOneWayMessage){
            if(message_length > MAX_RPC_SIZE-1) {
                //this message is too long - send back an error response
                signpost_processing_reply(source_address, message_type, 0);
            } else {
                uint8_t test_increment = increment_head();
                if(test_increment == rpc_queue_tail) {
                    //the queue is full send back an error message
                    signpost_processing_reply(source_address, message_type, 0);
                } else {
                    //alright this should be good
                    rpc_queue[rpc_queue_head][0] = source_address;
                    memcpy(rpc_queue[rpc_queue_head]+1, message,message_length);

                    //set the Edison rpc process pin to high
                    //wakeup the edison
                }

            }
        } else if(message_type == ProcessingTwoWayMessage) {
            //not currently supported
        } else {
            //unexpected - drop
        }
    } else {
        //unexpected drop
    }

}

int main (void) {
  printf("\n[Storage Master]\n** Main App **\n");
  
  gpio_enable_output(RPC_PENDING_PIN);
  gpio_clear(RPC_PENDING_PIN);

  // set up the SD card and storage system
  int rc = storage_initialize();
  if (rc != SUCCESS) {
    printf(" - Storage initialization failed\n");
    return rc;
  }

  // turn off Red Led
  led_off(DEBUG_RED_LED);

  // Install hooks for the signpost APIs we implement
  static api_handler_t storage_handler = {StorageApiType, storage_api_callback};
  static api_handler_t processing_handler = {ProcessingApiType, processing_api_callback};
  static api_handler_t* handlers[] = {&storage_handler, &processing_handler};
  do {
    rc = signpost_initialization_storage_master_init(handlers);
    if (rc < 0) {
      printf(" - Error initializing bus access (code: %d). Sleeping 5s\n", rc);
      delay_ms(5000);
    }
  } while (rc < 0);

  // Setup watchdog
  //app_watchdog_set_kernel_timeout(30000);
  //app_watchdog_start();

  printf("\nStorage Master initialization complete\n");
}

