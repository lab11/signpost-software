Signpost Tools
==============

This directory holds tools for debugging and using signpost.

## Debug Radio

Scripts for allowing a debug backplane to function as if there were a radio
module. In practice it sends data over the serial port to your computer, which
posts it to the signpost backend.

## Static BLE

Allows one to send data from the signpost over BLE instead of LoRa or Cellular.
Currently requires special signpost radio configuration to function. 

## Summon

A [Summon](https://github.com/lab11/summon) application for viewing signpost
data on your phone.

## Test Data

Code for sending test data on the internal MQTT stream. Currently for internal
use only, but could be extended in the future.

## Signbus

A python library for sending messages on the bus. Not currently functional
