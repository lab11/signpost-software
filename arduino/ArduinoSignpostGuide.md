
Arduino + Signpost Installation Guide
=====================================
This guide details how to setup an Arduino for use with the Signpost platform.
## Software Setup
### Step 1. 
Download and install the Arduino IDE from [here](https://www.arduino.cc/en/Main/Software)
### Step 2.
Open the IDE and go to `Tools` > `Board` > `Boards Manager`

Install version `1.6.15` of the `Arduino SAMD Boards (32-bits ARM Cortex M3)` board package
### Step 3. 
Clone this repository:
```bash
git clone https://github.com/lab11/signpost-software/
```
### Step 4.
cd into signpost-software/arduino

Run the `build_arduino_package` bash script

#### Arguments:
##### -d
- Builds optional developer package
- Creates symlinks to original files in repo instead of making copies of them
- Any changes made to files in repo will immediately propogate to the Arduino Signpost library
- Script will not work if PATH_TO_ARDUINO_SKETCHBOOK has any spaces in it (Adding '\ ' doesn't work either)
- Only supported on Linux
##### PATH_TO_ARDUINO_SKETCHBOOK
- Specifies path to the sketchbook for the Arduino IDE
- This is where the Signpost library will be installed
- All files will be installed in PATH_TO_ARDUINO_SKETCHBOOK/hardware/signpost

The Arduino sketchbook is usually located in:
- Windows: ~/Documents/Arduino
- Mac: ~/Documents/Arduino
- Linux: ~/Arduino

```bash
syntax: ./build_arduino_package [-d] PATH_TO_ARDUINO_SKETCHBOOK
ex: ./build_arduino_package ~/Arduino
```
### Step 5.
Back in the IDE, navigate to `Tools` > `Board` and select `Arduino MKRZero Signpost`

Open `module_test.ino` located in `signpost-software/apps/arduino_module`

This is a simple test app that requests time data from the controller and prints it onto the serial monitor.

For now, hit the verify button in the top left corner to make sure that it compiles.

If the app fails to compile, double check the previous steps.

## Hardware Setup
### Step 1.
##### Gather the following:
- 1 x Arduino MKRZero
- 1 x Micro USB cable

Plug the board into your computer
### Step 2.
If you haven't done so already, follow the instructions [here](https://github.com/lab11/signpost-software/blob/master/docs/TutorialSession.md) to setup the backplane and controller module
### Step 3.
Wire the Arduino board to the Signpost backplane.

Currently, only the MKRZero is the only supported Arduino board.

Hook up the MKRZero pins as follows:
MKRZero | Signpost Backplane
------------ | -------------
Pin 11 | SDA
Pin 12 | SCL
Pin 4  | IN
Pin 5  | OUT

## Test It

Make sure that controller module is powered and running `signpost_controller_app`.

Plug the Arduino MKRZero into the serial port of your computer.

Go back to the IDE and re-open `module_test.ino`.

In the IDE, go to `Tools` > `Port` and select the serial port associated with the Arduino.

Hit the upload button in the top left corner to load the code onto the board.

Open the serial monitor under `Tools` > `Serial Monitor` to view the data being sent over the serial port.

Make sure that the Serial monitor is set to 9600 Baud.

If setup properly, time data should be displayed once a second on the serial monitor.

If the module fails to initialize, try hitting the reset button on the Arduino.

The module can take up to roughly 15 seconds to initialize.

## Tips

- The MKRZero can switch serial ports often, so always double check which serial port the IDE is connected to when uploading code or opening the serial monitor
- If the IDE hangs while uploading code, double tap the reset button on the MKRZero to put it into bootloader mode
- Go to `File` > `Preferences` > `Show verbose output during: ` and check both boxes to make it easier to debug the above issues

## Documentation

[*Arduino Reference Page*](https://www.arduino.cc/en/Reference/HomePage)

[*Arduino MKRZero Product Info*](https://store.arduino.cc/usa/arduino-mkrzero)

[*Signpost Tutorial*](https://github.com/lab11/signpost-software/blob/master/docs/TutorialSession.md)

[*Signpost API Guide*](https://github.com/lab11/signpost-software/blob/master/docs/ApiGuide.md)
