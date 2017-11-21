Getting Started with the Signpost API
=====================================

The signpost API runs on signpost modules and handles initialization while
providing modules with access to shared services such as networking,
storage, processing, energy, time and location. This prevents the module
developer from needing to interact with the shared I2C bus directly, or
knowing the message formats. This document is a simple guide to using
the APIs. To use these APIs you must

```c
#include "signpost_api.h"
```

at the top of your application.

All Signpost API calls return an error code, which can be used
to determine success or failure.

## Contents
1. [Initialization](#initialization)
2. [Storage](#storage)
3. [Networking](#networking)
4. [Time](#time)
5. [Location](#location)
6. [Energy](#energy)

## Initialization

Initialization registers a signpost module with the controller and sets up
shared symmetric keys with other modules. For now, this method could take a few seconds to run
because it must wait for the controller to perform key
exchange. If you are providing a service to other signpost modules, you would also
declare that service here.

The prototype of the initialization function is:

```c
int signpost_init(char* module_name);
```

The "module\_name" string should be unique to your module type and 
sixteen characters or less. This name will be used to identify
your module throughout the signpost stack.

This is the first Signpost API function every module should call.

## Storage

Modules can storage data on an SD card that is on the control module.

They can access this SD card through the storage API. Currently modules
can only write to the SD card in an append only log. This would be good for
storing long term data, and allowing the Intel Edison 
or other modules to access and process that data. 

To store data to the SD card you must have a log name (which is in
practice the file name on the SD cards FAT32 file system). Logs for
your module are store in the file "module\_name/log\_name". 

Currently you may read, write and delete your own logs, and a 
module may see and read the logs of other modules. Log names
containing forward slashes will fail unless they successfully
read the log of another module.

To store data to the SD card then retrieve it: 

```c
uint8_t* data = {0x01, 0x02, 0x03};
Storage_Record_t record;
strcpy(record.logname, "my_log");
int result = signpost_storage_write(data, 3, &record);
if(result < SIGNPOST_ERROR) { //handle error}

uint8_t* read_data[3];
int result = signpost_storage_read(read_data, 3, &record);
```

To read the data from another module (assuming the data exists):

```c
uint8_t* read_data[3];
Storage_Record_t record;
strcpy(record.logname, "other_module/log");
record.offset = 0;
record.length = 3;
int result = signpost_storage_read(read_data, 3, &record);
```


## Networking

Currently the signpost API provides an HTTP POST abstraction, and
a pub/sub abstraction. 


### Pub/Sub

We recommend the pub/sub abstraction because 
it supports both cellular and lora networks (and is therefore both
more reliable and can be lower power). 

```c
//int signpost_networking_publish(char* topic, uint8_t* data, uint8_t data_len);

uint8_t* data = {0x01, 0x02, 0x03};

//will appear at signpost/mac_address/module_name/my_topic
int result = signpost_networking_publish("my_topic", data, 3);
```

where topic is a string less than 12 characters, and data is a buffer less
than 90 bytes. You can retrieve published data by subscribing to
the signpost MQTT stream at signpost/mac\_address/module\_name/topic. Please
see the additional [Signpost Networking Architecture](https://github.com/lab11/signpost-software/blob/master/docs/NetworkArch.md) 
for more information about integrating with the Signpost backend. 

Just as you can receive data from Signpost MQTT stream, it can
also be used send data to modules by publishing to signpost/mac\_address/module\_name/topic.
Modules can receive this data by subscribing to these messages with
a callback:

```c
//int signpost_networking_subscribe(subscribe_callback_type* cb);

void subscribe_callback(char* topic, uint8_t* data, uint8_t data_len) {
    //process the topic and data here
}

//subscribe to incoming data
signpost_networking_subscribe(subscribe_callback);
```

The module\_name/update topic is reserved and used by the signpost API to 
trigger the software update process.

We hope to enable internal module-to-module messaging through
this pub/sub services as well, and are planning this in a future release.

### HTTP Post

The HTTP Post method is meant to be a simple way of sending data outside
the Signpost ecosystem. To allow for end-to-end responses, it uses
the cellular modem on Signpost, and is therefore always higher power and
potentially less reliable than the Pub/Sub methods in situations where
a LoRa network is available but cellular coverage is poor.

Currently we only support extremely simple HTTP Post, which posts
data with an octetstream content-type and only returns the status code.

```c
//int signpost_networking_post(char* url, uint8_t* data, uint16_t data_len);

uint8_t* data = {0x01, 0x02, 0x03};

//result is either a signpost error, or a valid http result code
int result = signpost_networking_post("httpbin.org/post", data, 3);
```

We may expand the HTTP post method to include responses in the future.

### Other Networking Notes

Currently the networking is not implemented to be real-time. It does not
offer any determinism in delay times, nor does it have mechanisms to
elevate packet priority for immediate transmission. All networking is implemented
as a FIFO queue,
and packets will on average incur a 3-5s delay, but the delay could be much
longer in the case of high network load or failure. If your application
needs real-time networking let us know in an issue.

## Time

The time API returns current time. It returns GPS time (which is notably off
from UTC by ~9 seconds at time of writing) as specified by the "time.h"
library in either calendar time or unix time.

To use the time API declare a time structure and call the time API functions:

```c
#include <time.h>

//int signpost_timelocation_get_time(time_t* time);

time_t time;
int result_code = signpost_timelocation_get_time(&time);
if(result_code == SIGNPOST_ENOSAT) {
    //time invalid due to lack of satellites
}

struct tm* calendar_time;
result_code = signpost_timelocation_get_calendar_time(calendar_time);
if(result_code == SIGNPOST_ENOSAT) {
    //time invalid due to lack of satellites
}
```

The time API handles synchronization for you. It uses the PPS line and 
a timer to ensure that the time it returns is the current GPS time. Apps
can then be sure that the next PPS occurs on the returned time + 1s, and
use this for global time synchronization.

Note that when packets are *received* in the pub/sub system they are timestamped,
however there may be a delay (average 3-5s, but potentially minutes) between
when the modules sends data and when it is received as noted above. 

## Location

The location API provides location from the GPS. To use the location API:

```c
signpost_timelocation_location_t location;
int result_code = signpost_timelocation_get_location(&location);
if(result_code == SIGNPOST_ENOSAT) {
    //location invalid due to lack of satellites
}
```

The location structure provides the fields:

```c
typedef struct __attribute__((packed)) {
    float latitude;
    float longitude;
    uint8_t satellite_count;
} signpost_timelocation_location_t;
```

Location is valid if `satellite_count` >= 4.

Note that apps do not need to explicitly send the current location if
using the pub/sub system because a current\_location tag, which is
the average location of a signpost over the past two hours, is appended
to all signpost packets.

## Energy

Signpost modules may need to adapt to varying energy conditions, and
may want to be turned off for periods of time to save their
energy allocations. You can read the [Energy Allocation Strategy](https://github.com/lab11/signpost-software/blob/master/docs/Energy.md)
for more information on Signpost  energy policies.

The API consists of a concept akin to an energy timer and an overall energy budget.
Modules can read and reset their timer for internal use (such as to
measure their average energy over time), and modules can read
their total energy budget to make sure they don't exceed available energy.
Modules can also request to be duty-cycled, or powered off and turned on
again at some time in the future.

To use the energy query API:

```c
signpost_energy_information_t energy;
int result = signpost_energy_query(&energy);
```

where the energy structure is:

```c
typedef struct __attribute__((packed)) energy_information {
    uint32_t    energy_used_since_reset_uWh;
    uint32_t    energy_limite_uWh;
    uint32_t    time_since_reset_s;
    uint8_t     energy_limit_warning_threshold;
    uint8_t     energy_limit_critical_threshold;
} signpost_energy_information_t;
```

and to use the duty cycle API:

```c
//int signopst_energy_duty_cycle(uint32_t time_in_s)

int result = signpost_energy_duty_cycle(600);
```
