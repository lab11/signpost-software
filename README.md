Signpost Software
=================

[![Build Status](https://travis-ci.org/lab11/signpost-software.svg?branch=master)](https://travis-ci.org/lab11/signpost-software)

This repository holds all of the software for the [Signpost City-Scale
sensing project](https://github.com/lab11/signpost). Please see that 
repository for more information on the project as a whole. 

The software for the Signpost project mainly consists of code
that runs on the Cortex-M class microcontrollers that we use
on the Signpost modules. Some of the software runs on Signpost
*core* modules (such as the radio module and control module) and provides
resources including networking, time, location and storage to the sensor
modules. Sensor modules can access these resources through the *Signpost API*,
which defines the full networking stack for interacting with a Signpost.
The goal of the Signpost API is to significantly simplify sensor
module applications.


## Signpost API

The Signpost API exists as a shim between the application and the hardware
on your microcontroller, and can easily be ported to multiple platforms
by implementing several calls to I2C, GPIO and Timer peripherals.
Currently most sensors (and the core modules) run [Tock](https://github.com/helena-project/tock),
but we currently have one sensor module running ARM Mbed, and we have an
Arduino port working and nearly ready to merge.

<img src="https://raw.githubusercontent.com/lab11/signpost/master/media/signpost_software_transparent.png" width="70%" />

## Current Status

The API is still in slight flux, but is starting to stabilize for a 'Signpost 1.0'
release. We are primarily still working on over the air updates. Please 
feel free to contact us at <signpost-admin@eecs.berkeley.edu> if you have
any questions!

## Current Organization

#### Signpost
All code running on the signpost is in this folder.

#### docs
Documentation that is currently being updated

#### Server
The signpost backend and lab11-specific packet parsers.

#### tools
Tools that run on your desktop to help with signpost development.



## License

This repository inherits its licensed from the parent project. Please
see the [signpost repository](https://github.com/lab11/signpost#license) for
the license statement.
