Mbed example app
================
This is an example app that works with mbed os.

It initializes with a signpost then blinks the debug LED.

To use hardware that runs MBED OS you must:
 1) Copy this folder structure
  - a .mbed file that describes the directories and targets
  - a symlink to signpost-software/apps/support/mbed/mbed-os
 2) define the hardware pins in board.h
  - Use the board.h file in this directory as a template

The building and flashing system works just as it does for other signpost apps.
