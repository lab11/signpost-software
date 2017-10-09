# Base folder definitions
TOCK_USERLAND_BASE_DIR ?= ../../../kernel/tock/userland/
SIGNPOST_USERLAND_BASE_DIR ?= ../..
LIBNAME := fatfs
$(LIBNAME)_DIR := $(SIGNPOST_USERLAND_BASE_DIR)/support/$(LIBNAME)

# List all C and Assembly files
#$(LIBNAME)_SRCS  := $(wildcard $($(LIBNAME)_DIR)/internal/*.c) $(wildcard $($(LIBNAME)_DIR)/*.c) $(wildcard $($(LIBNAME)_DIR)/*.s)
$(LIBNAME)_SRCS := $($(LIBNAME)_DIR)/ff.c $($(LIBNAME)_DIR)/option/unicode.c

include $(TOCK_USERLAND_BASE_DIR)/TockLibrary.mk
