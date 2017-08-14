#include <stdint.h>

#include <Wire.h>

#include "signpost_api.h"
//This is an example i2c address. It can be modified to fit whatever you application happens to be.
#define ARDUINO_MODULE_I2C_ADDRESS 0x48

void setup() {
  Serial.begin(9600);
  Serial.print("Program Start\n");
  int rc = signpost_initialization_module_init(ARDUINO_MODULE_I2C_ADDRESS, NULL);
  while (rc != 0) {
    char msg[100];
    sprintf(msg, " - Error initializing bus (code %d). Sleeping for 5s\n", rc);
    Serial.print(msg);
    rc = signpost_initialization_module_init(ARDUINO_MODULE_I2C_ADDRESS, NULL);
    delay(5000);
  }
  Serial.print("Intialization Complete\n");
  delay(1000);
}

void loop() {
  signpost_timelocation_time_t t;
  int result_code = signpost_timelocation_get_time(&t);
  
  Serial.print("__________________________________________________________\n");
  char output[100];
  //sprintf("%d:%d:%d %d/%d/%d Satellites: %d", output, t.hours, t.minutes, t.seconds,
                                              //t.month, t.day, t.year, t.satellite_count);
  //Serial.print(output);
  Serial.print(t.hours);
  Serial.print(":");
  Serial.print(t.minutes);
  Serial.print(":");
  Serial.print(t.seconds);
  Serial.print(" ");
  Serial.print(t.month);
  Serial.print("/");
  Serial.print(t.day);
  Serial.print("/");
  Serial.print(t.year);
  Serial.print(" Satellites: ");
  Serial.print(t.satellite_count);
  Serial.print('\n');
  delay(1000);
  
}
