#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_FNAME_LEN 255

// function prototypes

// convert logname to used filename
// first 2 characters are module address
// 'XX<logname>'
void logname_to_filename(char* logname, uint8_t mod_addr, char* filename);

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
