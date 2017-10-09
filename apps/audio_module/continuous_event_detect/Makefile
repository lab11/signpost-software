# makefile for user application

# the current directory
APP_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# files needed for this code
VPATH_DIRS += ../../support/log2fix
VPATH_DIRS += ../../support/kiss_fft
VPATH_DIRS += ../../support/kiss_fft/tools

# we're adding these to the local path instead
#VPATH_DIRS += ../../support/audio_class/c

C_SRCS   += main.c
C_SRCS   += parameters.c
C_SRCS   += kiss_fft.c
C_SRCS   += kiss_fftr.c
C_SRCS   += log2fix.c
C_SRCS   += ridgeTracker.c
C_SRCS   += dynArray.c

override CFLAGS += -DFIXED_POINT=16
override CFLAGS += -Wno-type-limits -Wno-sign-compare
override CFLAGS += -I$(APP_DIR)/../../support/kiss_fft
override CFLAGS += -I$(APP_DIR)/../../support/kiss_fft/tools
override CFLAGS += -I$(APP_DIR)../../support
override CFLAGS += -I$(APP_DIR)/../../support/audio_class/c

APP_HEAP_SIZE := 4000
STACK_SIZE := 2048

TOCK_BOARD = audio_module

# include makefile settings that are shared between applications
include ../../AppMakefileTock.mk

