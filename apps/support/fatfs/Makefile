# Base folder definitions
TOCK_USERLAND_BASE_DIR ?= ../../../kernel/tock/userland/
LIBNAME := fatfs
$(LIBNAME)_DIR := $(TOCK_USERLAND_BASE_DIR)/../../../apps/support/$(LIBNAME)

# List all C and Assembly files
$(LIBNAME)_SRCS  := $(wildcard $($(LIBNAME)_DIR)/internal/*.c) $(wildcard $($(LIBNAME)_DIR)/*.c) $(wildcard $($(LIBNAME)_DIR)/*.s)

include $(TOCK_USERLAND_BASE_DIR)/TockLibrary.mk
