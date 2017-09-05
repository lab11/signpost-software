#This makefile is a supporting makefile for mbed apps
#It sets up variables so that it can call the Linux-specific JLinkMakefile

OUTPUT_PATH = $(APP_DIR)/build/$(TARGET)/$(TOOLCHAIN)/
OUTPUT_BIN :=  libsignpost-mbed.bin

CURRENT_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
SIGNPOST_APP_DIR := $(abspath $(CURRENT_DIR))

MBED_LIB = $(SIGNPOST_APP_DIR)/libsignpost-mbed
SIGNPOST_LIB = $(SIGNPOST_APP_DIR)/libsignpost
MBEDTLS_INC = $(SIGNPOST_APP_DIR)/support/mbedtls/mbedtls/include
MBEDTLS_SRC = $(SIGNPOST_APP_DIR)/support/mbedtls/mbedtls/library
BUILD_PROFILE = $(SIGNPOST_APP_DIR)/support/mbed/build_profile.json

ifeq ($(TARGET),NUCLEO_L432KC)
FLASH_START_ADDRESS = 0x08000000
JLINK_DEVICE = STM32L432KC
else
$(error Platform not know. Please add flash start address to AppMakefileMbed.mk)
endif

MBED_CHECK := $(shell mbed --version 2> /dev/null)

all: 
ifndef MBED_CHECK
	$(error You must install the mbed-cli tools (try pip install mbed-cli))
else
	mbed compile -m $(TARGET) --source $(MBED_LIB) --source $(APP_DIR) $(APP_COMPILE_FLAGS) --source $(SIGNPOST_LIB) --source $(MBEDTLS_INC) --source $(MBEDTLS_SRC) -t $(TOOLCHAIN) --build $(OUTPUT_PATH) --profile $(BUILD_PROFILE)
endif

JLINK_OPTIONS = -device $(JLINK_DEVICE) -if swd -speed 1000

################################################################################
## The rest of the makefile works with *nix based systems for flashing
## using JLinkExe
################################################################################

# Set toolchain path if arm-none-eabi- binaries are not in user path
# TOOLCHAIN_PATH ?=
TERMINAL ?= gnome-terminal -e

ifdef SEGGER_SERIAL
JLINKEXE_OPTION = -SelectEmuBySn $(SEGGER_SERIAL)
JLINKGDBSERVER_OPTION = -select USB=$(SEGGER_SERIAL)
JLINKRTTCLIENT_OPTION = -select USB=$(SEGGER_SERIAL)
endif

MAKE_BUILD_FOLDER = mkdir -p $(OUTPUT_PATH)

JLINK = -JLinkExe $(JLINK_OPTIONS) $(JLINKEXE_OPTION)
JLINKGDBSERVER = JLinkGDBServer $(JLINK_OPTIONS) $(JLINKGDBSERVER_OPTION)
JLINKRTTCLIENT = JLinkRTTClient $(JLINK_OPTIONS) $(JLINKRTTCLIENT_OPTION)

clean:
	rm -rf $(OUTPUT_PATH)
	rm -f *.jlink
	rm -f JLink.log
	rm -f .gdbinit

flash: all flash.jlink
	$(JLINK) $(OUTPUT_PATH)flash.jlink

flash.jlink:
	$(MAKE_BUILD_FOLDER)
	printf "r\n" > $(OUTPUT_PATH)flash.jlink
	printf "loadbin $(OUTPUT_PATH)/$(OUTPUT_BIN), $(FLASH_START_ADDRESS) \nr\ng\nexit\n" >> $(OUTPUT_PATH)flash.jlink

reset: reset.jlink
	$(MAKE_BUILD_FOLDER)
	$(JLINK) $(OUTPUT_PATH)reset.jlink

reset.jlink:
	$(MAKE_BUILD_FOLDER)
	printf "r\ng\nexit\n" > $(OUTPUT_PATH)reset.jlink

erase: erase.jlink
	$(MAKE_BUILD_FOLDER)
	$(JLINK) $(OUTPUT_PATH)erase.jlink

erase.jlink:
	$(MAKE_BUILD_FOLDER)
	printf "erase\nexit\n" > $(OUTPUT_PATH)erase.jlink

#I don't know how to make these portable but they would be nice to add
#The current implementations are stolen from github.com/lab11/nrf5x-base
#recover: recover.jlink erase-all.jlink pin-reset.jlink
#	$(MAKE_BUILD_FOLDER)
#	$(JLINK) $(OUTPUT_PATH)recover.jlink
#	$(JLINK) $(OUTPUT_PATH)erase-all.jlink
#	$(JLINK) $(OUTPUT_PATH)pin-reset.jlink
#
#recover.jlink:
#	$(MAKE_BUILD_FOLDER)
#	printf "si 0\nt0\nsleep 1\ntck1\nsleep 1\nt1\nsleep 2\nt0\nsleep 2\nt1\nsleep 2\nt0\nsleep 2\nt1\nsleep 2\nt0\nsleep 2\nt1\nsleep 2\nt0\nsleep 2\nt1\nsleep 2\nt0\nsleep 2\nt1\nsleep 2\nt0\nsleep 2\nt1\nsleep 2\ntck0\nsleep 100\nsi 1\nr\nexit\n" > $(OUTPUT_PATH)recover.jlink
#
#pin-reset.jlink:
#	$(MAKE_BUILD_FOLDER)
#	printf "w4 40000544 1\nr\nexit\n" > $(OUTPUT_PATH)pin-reset.jlink
#
#pin-reset: pin-reset.jlink
#	$(MAKE_BUILD_FOLDER)
#	$(JLINK) $(OUTPUT_PATH)pin-reset.jlink
#
#erase-all: erase-all.jlink
#	$(MAKE_BUILD_FOLDER)
#	$(JLINK) $(OUTPUT_PATH)erase-all.jlink
#
#erase-all.jlink:
#	# Write to NVMC to enable erase, do erase all, wait for completion. reset
#	$(MAKE_BUILD_FOLDER)
#	printf "w4 4001e504 2\nw4 4001e50c 1\nsleep 100\nr\nexit\n" > $(OUTPUT_PATH)erase-all.jlink

#startdebug: debug-gdbinit
#	$(TERMINAL) "$(JLINKGDBSERVER) -port $(GDB_PORT_NUMBER)"
#	sleep 1
#	$(TERMINAL) "$(GDB) $(ELF)"
#
#startrtt:
#	$(TERMINAL) "JLinkExe -device nrf51822 -if swd -speed 1000 -AutoConnect 1"
#	sleep 1
#	$(TERMINAL) "$(JLINKRTTCLIENT)"
#
#debug-gdbinit:
#	printf "target remote localhost:$(GDB_PORT_NUMBER)\nload\nmon reset\nbreak main\n" > .gdbinit
#

.PHONY: flash flash.jlink
