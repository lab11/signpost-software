# Base folder definitions
CURRENT_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
SIGNPOST_USERLAND_BASE_DIR := $(abspath $(CURRENT_DIR)../../)
TOCK_USERLAND_BASE_DIR := $(abspath $(CURRENT_DIR)../../libtock-c/)


LIBNAME := fatfs
$(LIBNAME)_DIR := $(SIGNPOST_USERLAND_BASE_DIR)/support/$(LIBNAME)

# List all C and Assembly files
#$(LIBNAME)_SRCS  := $(wildcard $($(LIBNAME)_DIR)/internal/*.c) $(wildcard $($(LIBNAME)_DIR)/*.c) $(wildcard $($(LIBNAME)_DIR)/*.s)
$(LIBNAME)_SRCS := $($(LIBNAME)_DIR)/ff.c $($(LIBNAME)_DIR)/option/unicode.c

include $(TOCK_USERLAND_BASE_DIR)/TockLibrary.mk
