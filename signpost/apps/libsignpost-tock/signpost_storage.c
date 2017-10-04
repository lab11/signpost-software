#include <stdio.h>
#include <stdlib.h>
#include <led.h>
#include <timer.h>
#include <sdcard.h>
#include <tock.h>

#include "ff.h"

#include "signpost_api.h"
#include "signpost_storage.h"

FATFS fs;           /* File system object */

static FRESULT scan_files (const char* path) {
    FRESULT res;
    DIR dir;
    static FILINFO fno;

    res = f_opendir(&dir, path);                       // Open the directory
    if (res == FR_OK) {
      while(1) {
        res = f_readdir(&dir, &fno);                   // Read a directory item
        if (res != FR_OK || fno.fname[0] == 0) break;  // Break on error or end of dir
        if (fno.fattrib & AM_DIR) {                    // It's a directory
          printf("+ %s/\n", fno.fname);
        } else {                                       // It's a file
          printf("  %s\n", fno.fname);
        }
      }
      f_closedir(&dir);
    }

    return res;
}

int32_t storage_write_data (const char* filename, uint8_t* buf, size_t buf_len, size_t bytes_to_write, size_t* bytes_written, size_t* offset)
{
  size_t len = buf_len < bytes_to_write? buf_len : bytes_to_write;
  FIL fp;
  FRESULT res;

  // XXX check valid filename
  for (int i = 0; i < STORAGE_LOG_LEN; i++) {
    if (filename[i] == '/') return TOCK_EINVAL;
  }
  if (strnlen(filename, STORAGE_LOG_LEN) == STORAGE_LOG_LEN) return TOCK_ESIZE;

  // determine folder names and check for existence, create if don't exist
  //char* temp_filename = (char*) malloc(STORAGE_LOG_LEN+1);
  //temp_filename[STORAGE_LOG_LEN] = 0;
  //strncpy(temp_filename, filename, STORAGE_LOG_LEN);
  //char *found;
  //char full_path[STORAGE_LOG_LEN];
  //size_t full_path_index = 0;

  //while((found = strsep((char**) &temp_filename, "/")) != NULL) {
  //  if (temp_filename == NULL) break;
  //  printf("found %s\n", found);
  //  size_t found_len = strnlen(found, STORAGE_LOG_LEN);
  //  // check full path is not too large
  //  if (full_path_index + 1 + found_len > STORAGE_LOG_LEN) return TOCK_ESIZE;
  //  // copy new dir/file name to full path
  //  memcpy(full_path + full_path_index, found, found_len);
  //  full_path_index += found_len;
  //  // make directory
  //  res = f_mkdir(full_path);
  //  if (res != FR_OK && res != FR_EXIST) {
  //    return TOCK_EINVAL;
  //  }
  //  // add dir seperator to full path
  //  full_path[full_path_index] = '/';
  //  full_path_index += 1;

  //  // check if the next bit is the actual filename at end of path
  //  size_t i;
  //  for(i = full_path_index; i < STORAGE_LOG_LEN; i++) {
  //    if (filename[i] == '\0' || filename[i] == '/') break;
  //  }
  //  if (filename[i] == '\0') break;
  //}
  //free(temp_filename);

  // open file for append and write
  res = f_open(&fp, filename, FA_OPEN_APPEND | FA_WRITE);
  printf("%d\n", res);
  if (res != FR_OK) return TOCK_FAIL;

  // copy file pointer to offset
  *offset = fp.fptr;

  // write len bytes of buf to file
  res = f_write(&fp, buf, len, bytes_written);
  if (res != FR_OK) return TOCK_FAIL;

  // close file
  res = f_close(&fp);
  if (res != FR_OK) return TOCK_FAIL;

  return TOCK_SUCCESS;
}

int32_t storage_read_data (const char* filename, size_t offset, uint8_t* buf, size_t buf_len, size_t bytes_to_read, size_t* bytes_read)
{
  size_t len = buf_len < bytes_to_read? buf_len : bytes_to_read;
  FIL fp;

  // open file for read
  FRESULT res = f_open(&fp, filename, FA_READ);
  if (res != FR_OK) return TOCK_FAIL;

  // advance pointer to offset
  res = f_lseek(&fp, offset);
  if (res != FR_OK) return TOCK_FAIL;

  // perform read of len bytes to buf
  res = f_read(&fp, buf, len, bytes_read);
  if (res != FR_OK) return TOCK_FAIL;

  // close file
  res = f_close(&fp);
  if (res != FR_OK) return TOCK_FAIL;

  return TOCK_SUCCESS;
}

int32_t storage_del_data (const char* filename) {
  FRESULT res = f_unlink(filename);
  if (res != FR_OK) return TOCK_FAIL;

  return TOCK_SUCCESS;
}

int32_t storage_initialize (void) {
  FRESULT res = f_mount(&fs, "", 1);

  while (res != FR_OK) {
    switch (res) {
      case FR_NOT_READY:
        printf("No disk found. Trying again...\n");
        delay_ms(5000);
        res = f_mount(&fs, "", 1);
        break;
      case FR_NO_FILESYSTEM:
        printf("No filesystem. Running mkfs...\n");
        {
          BYTE* work = malloc(_MAX_SS);
          res = f_mkfs("", FM_ANY, 0, work, _MAX_SS);
          if (res != FR_OK) {
            printf("Failed to mkfs: %d\n", res);
            return TOCK_FAIL;
          }
          free(work);
        }
        res = f_mount(&fs, "", 1);
        if (res != FR_OK) {
          printf("Failed to mount created fs: %d\n", res);
          return TOCK_FAIL;
        }
        break;
      default:
        printf("Unexpected error! %d\n", res);
        return TOCK_FAIL;
    }
  }

  printf("=== Mounted! Scanning root directory...\n");
  scan_files("");
  printf("=== Done.\n");

  return TOCK_SUCCESS;
}

