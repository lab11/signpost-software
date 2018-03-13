# makefile for user application

TOCK_BOARD = storage_master

# the current directory
APP_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# files needed for this code
C_SRCS   := main.c


# include makefile settings that are shared between applications
include ../../../support/fatfs/Makefile
include ../../../AppMakefileTock.mk

# To see the generated rules, run:
# $(info $(foreach arch,$(TOCK_ARCHS),$(call FATFS_RULES,$(arch))))
# Actually generate the rules for each architecture
$(foreach arch, $(TOCK_ARCHS), $(eval $(call FATFS_RULES,$(arch))))

