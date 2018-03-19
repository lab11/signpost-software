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

## Contents
1. [Initialization](#initialization)
2. [Storage](#storage)
3. [Networking](#networking)
4. [Simple Networking](#simple-networking)
5. [Posting to GDP](#posting-to-gdp)
6. [Energy](#energy)
7. [Time](#time)
8. [Location](#location)
9. [Processing](#processing)

## Initialization

Initialization registers a signpost module with the controller and sets up
shared symmetric keys with other modules. Modules initialize with a name 
(16 characters or less), and this name is used to identify
the module throughout the signpost ecosystem. This is the first Signpost API 
function every module must call.

The prototype of the initialization function is:

```c
int signpost_init(char* module_name);
```

There also exists a more complex initialization function used to provide
services. This is used by the control module, radio module and storage
module, but could be used by users for multi-module applications.

## Storage

Currently being updated.

## Networking

Currently being updated

## Time

Currently being updated.

## Location

The location API provides location from the GPS. To use the location API:

```c
signpost_timelocation_location_t location;
int result_code = signpost_timelocation_get_location(&location);
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

## Energy

Signpost modules may need to adapt to varying energy conditions, and
may want to be turned off for periods of time to save their
energy allocations. 

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
