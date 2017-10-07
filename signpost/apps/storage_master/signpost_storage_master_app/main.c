#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <i2c_master_slave.h>
#include <led.h>
#include <sdcard.h>
#include <timer.h>
#include <tock.h>

#include "app_watchdog.h"
#include "signpost_api.h"
#include "signpost_storage.h"
#include "storage_master.h"
#include "port_signpost.h"

#define DEBUG_RED_LED 0

static void storage_api_callback(uint8_t source_address,
    signbus_frame_type_t frame_type, signbus_api_type_t api_type,
    uint8_t message_type, size_t message_length, uint8_t* message) {
  int err = TOCK_SUCCESS;

  if (api_type != StorageApiType) {
    signpost_api_error_reply_repeating(source_address, api_type, message_type, SB_PORT_EINVAL, true, true, 1);
    return;
  }

  if (frame_type == NotificationFrame) {
    // XXX unexpected, drop
  } else if (frame_type == CommandFrame) {
    printf("\nGot a command message!: len = %d\n", message_length);
    for (size_t i=0; i<message_length; i++) {
      printf("%2x", message[i]);
    }
    printf("\n");

    if (message_type == StorageScanMessage) {
      if (message_length < sizeof(size_t)) {
        printf("Message length is too small\n");
        signpost_api_error_reply_repeating(source_address, api_type, message_type, TOCK_ESIZE, true, true, 1);
        return;
      }
      // unmarshal sent data into list_len
      size_t list_len = *(size_t*) message;
      printf("got request for %u records\n", list_len);
      Storage_Record_t* list = malloc(list_len * sizeof(Storage_Record_t));
      if (list == NULL) {
        printf("Not enough memory to store record list of length %u\n", list_len);
        signpost_api_error_reply_repeating(source_address, api_type, message_type, TOCK_ENOMEM, true, true, 1);
        return;
      }

      printf("Scanning root directory\n");

      // scan existing files
      err = storage_scan_files(list, &list_len, list_len);
      if (err < TOCK_SUCCESS) {
        printf("Scanning error: %d\n", err);
        signpost_api_error_reply_repeating(source_address, api_type, message_type, err, true, true, 1);
        free(list);
        return;
      }

      // send response
      err = signpost_storage_scan_reply(source_address, list, list_len);
      free(list);
      if (err < TOCK_SUCCESS) {
        signpost_api_error_reply_repeating(source_address, api_type, message_type, err, true, true, 1);
        return;
      }
    }

    else if (message_type == StorageWriteMessage) {
      // unmarshal sent data into logname and data
      char logname[STORAGE_LOG_LEN+1];
      strncpy(logname, (char*) message, STORAGE_LOG_LEN);
      size_t logname_len = strnlen(logname, STORAGE_LOG_LEN);
      // if the expected size of the message is greater than its length
      if (logname_len + sizeof(size_t) + 1 > message_length) {
        printf("Failed to allocate enough memory\n");
        signpost_api_error_reply_repeating(source_address, api_type, message_type, TOCK_ENOMEM, true, true, 1);
        return;
      }
      uint8_t* data = message + logname_len + 1;
      size_t data_len = message_length - logname_len - 1;

      printf("Writing data\n");

      // write data to storage
      Storage_Record_t write_record = {0};
      strncpy(write_record.logname, logname, STORAGE_LOG_LEN);
      write_record.length = data_len;
      size_t bytes_written = 0;

      err = storage_write_data(logname, data, data_len, data_len, &bytes_written, &write_record.offset);
      if (err < TOCK_SUCCESS || bytes_written < data_len) {
        printf("Writing error: %d\n", err);
        signpost_api_error_reply_repeating(source_address, api_type, message_type, err, true, true, 1);
        return;
      }

      // send response
      err = signpost_storage_write_reply(source_address, &write_record);
      if (err < TOCK_SUCCESS) {
        signpost_api_error_reply_repeating(source_address, api_type, message_type, err, true, true, 1);
        return;
      }
    }

    else if (message_type == StorageReadMessage) {
      // unmarshal sent data into logname, offset, and length
      char logname[STORAGE_LOG_LEN+1];
      strncpy(logname, (char*) message, STORAGE_LOG_LEN);
      size_t logname_len = strnlen(logname, STORAGE_LOG_LEN);
      // if the expected size of the message is greater than its length
      if (logname_len + sizeof(size_t)*2 + 2 > message_length) {
        printf("Failed to allocate enough memory\n");
        signpost_api_error_reply_repeating(source_address, api_type, message_type, TOCK_ENOMEM, true, true, 1);
        return;
      }
      size_t offset = *((size_t*) (message + logname_len + 1));
      for (size_t i=0; i<sizeof(offset); i++) {
        printf("%2x", *(message + logname_len + 1 + i));
      }
      printf("\n");
      size_t length = *((size_t*) (message + logname_len + 2 + sizeof(size_t)));
      for (size_t i=0; i<sizeof(length); i++) {
        printf("%2x", *(message + logname_len + 2 + sizeof(offset) + i));
      }
      printf("\n");
      //printf("%d %d %d\n", offset, length, logname_len);

      printf("Reading data\n");
      // read data from storage
      size_t bytes_read= 0;
      //XXX this is potentially dangerous, should eventually define limits:
      uint8_t* data = (uint8_t*) malloc(length);
      if (data == NULL) {
        printf("Failed to allocate enough memory\n");
        signpost_api_error_reply_repeating(source_address, api_type, message_type, TOCK_ENOMEM, true, true, 1);
        return;
      }

      err = storage_read_data(logname, offset, data, length, length, &bytes_read);
      if (err < TOCK_SUCCESS || bytes_read < length) {
        printf("Writing error: %d\n", err);
        signpost_api_error_reply_repeating(source_address, api_type, message_type, err, true, true, 1);
        free(data);
        return;
      }

      // send response
      err = signpost_storage_read_reply(source_address, data, length);
      free(data);
      if (err < TOCK_SUCCESS) {
        signpost_api_error_reply_repeating(source_address, api_type, message_type, err, true, true, 1);

        return;
      }
    }

    else if (message_type == StorageDeleteMessage) {
      char logname[STORAGE_LOG_LEN+1];
      strncpy(logname, (char*) message, STORAGE_LOG_LEN);

      printf("Deleting data\n");
      // delete logname from storage
      err = storage_del_data(logname);
      Storage_Record_t deleted_record = {0};
      strncpy(deleted_record.logname, logname, STORAGE_LOG_LEN);

      // send response
      err = signpost_storage_delete_reply(source_address, &deleted_record);
      if (err < TOCK_SUCCESS) {
        signpost_api_error_reply_repeating(source_address, api_type, message_type, err, true, true, 1);
        return;
      }
    }
  } else if (frame_type == ResponseFrame) {
    // XXX unexpected, drop
  } else if (frame_type == ErrorFrame) {
    // XXX unexpected, drop
  }
}

int main (void) {
  printf("\n[Storage Master]\n** Main App **\n");

  // set up the SD card and storage system
  int rc = storage_initialize();
  if (rc != TOCK_SUCCESS) {
    printf(" - Storage initialization failed\n");
    return rc;
  }

  // turn off Red Led
  led_off(DEBUG_RED_LED);

  // Install hooks for the signpost APIs we implement
  static api_handler_t storage_handler = {StorageApiType, storage_api_callback};
  static api_handler_t* handlers[] = {&storage_handler, NULL};
  do {
    rc = signpost_initialization_module_init(ModuleAddressStorage, handlers);
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

