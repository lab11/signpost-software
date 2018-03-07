#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "signpost_api.h"

// function prototypes

// scan files in root directory, return list of records and length of records
int32_t storage_scan_files(Storage_Record_t* list, size_t* list_len, size_t max_list_len);

// opens a file for writing and appends data to file
int32_t storage_write_data (const char* filename, uint8_t* buf, size_t buf_len, size_t bytes_to_write, size_t* bytes_written, size_t* offset);

// opens a file for reading and returns bytes_read number of bytes from offset in file
int32_t storage_read_data (const char* filename, size_t offset, uint8_t* buf, size_t buf_len, size_t bytes_to_read, size_t* bytes_read);

// deletes filename
int32_t storage_del_data (const char* filename);

// initializes the SD card and storage system on top of it
int32_t storage_initialize (void);

#ifdef __cplusplus
}
#endif
