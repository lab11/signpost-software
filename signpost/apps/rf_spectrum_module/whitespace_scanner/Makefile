#this Makefile should work with the mbed-cli to build the program,
#then it can use JLink to flash

# the current directory
APP_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

APP_COMPILE_FLAGS := --source $(APP_DIR)/../../support/RFExplorer_3GP_IoT_Arduino/src

#you must define a valid ARM MBed Target
TARGET = NUCLEO_L432KC

#you must define a vliad ARM Mbed toolchain (GCC_ARM or IAR)
TOOLCHAIN = GCC_ARM

include ../../AppMakefileMbed.mk
