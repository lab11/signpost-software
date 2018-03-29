# Makefile for user application

# Specify this directory relative to the current application.
APP_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Which files to compile.
C_SRCS := $(wildcard *.c)

STACK_SIZE=8192
APP_HEAP_SIZE=4096

# Set a default address for CI builds
ifeq ($(CI),true)
ID=0
endif

ifneq ($(ID),)
ADDRESS ?= c098e51200$(ID)
endif
ifneq ($(ADDRESS),)
C_ADDRESS ?= $(shell python -c "print('0x' + ',0x'.join([\"$(ADDRESS)\"[i:i+2] for i in range(0, len(\"$(ADDRESS)\"), 2 )]))")
endif


# The trick here is to only include the rule to build .address if the
# requisite variable is define, otherwise we define a "build rule" that
# fails with a debug message

ifeq ($(C_ADDRESS),)
.address:
	@echo "You must supply an ID (last octet) or full address"
	@exit 1
else
.PHONY: force_address
.address: force_address
	$(Q)bash -c "diff <(echo $(C_ADDRESS)) .address &>/dev/null || echo $(C_ADDRESS) > .address"
endif

override CFLAGS += -DCOMPILE_TIME_ADDRESS="$(C_ADDRESS)"

# Set a default key for CI builds
ifeq ($(CI),true)
PASSWORD=0
endif

ifneq ($(PASSWORD),)
KEY ?= $(shell python -c "print '0x' + ',0x'.join([elem.encode('hex') for elem in '$(PASSWORD)'])")
endif


# The trick here is to only include the rule to build .address if the
# requisite variable is define, otherwise we define a "build rule" that
# fails with a debug message

ifeq ($(KEY),)
.key:
	@echo "You must supply a password for the lora radio"
	@exit 1
else
.PHONY: force_key
.key: force_key
	$(Q)bash -c "diff <(echo $(KEY)) .key &>/dev/null || echo $(KEY) > .key"
endif

override CFLAGS += -DAPP_KEY="$(KEY)"


LIBRARY_PATHS += .

TOCK_USERLAND_BASE_DIR := ../../../kernel/tock/userland

# External libraries used
EXTERN_LIBS += $(TOCK_USERLAND_BASE_DIR)/libnrfserialization

# include makefile settings that are shared between applications
include ../../AppMakefileTock.mk

$(BUILDDIR)/main.o: .address

