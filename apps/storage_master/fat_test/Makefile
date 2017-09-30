# makefile for user application

TOCK_BOARD = storage_master

# the current directory
APP_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

VPATH_DIRS += $(APP_DIR)../../support/fatfs
VPATH_DIRS += $(APP_DIR)../../support/fatfs/option

# files needed for this code
C_SRCS   := main.c mmc_io.c
C_SRCS   += ff.c unicode.c

override CFLAGS += -I$(APP_DIR)/../../support/fatfs
override CFLAGS += -I$(APP_DIR)/../../support/fatfs/option

# include makefile settings that are shared between applications
include ../../AppMakefileTock.mk

# To see the generated rules, run:
# $(info $(foreach arch,$(TOCK_ARCHS),$(call FATFS_RULES,$(arch))))
# Actually generate the rules for each architecture
$(foreach arch, $(TOCK_ARCHS), $(eval $(call FATFS_RULES,$(arch))))

