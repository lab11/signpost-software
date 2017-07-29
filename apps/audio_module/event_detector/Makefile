# makefile for user application

# the current directory
APP_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# files needed for this code
VPATH_DIRS += log2fix
VPATH_DIRS += kiss_fft
VPATH_DIRS += kiss_fft/tools

C_SRCS   += kiss_fft.c
C_SRCS   += kiss_fftr.c
C_SRCS   += log2fix.c
C_SRCS   += ridgeTracker.c
C_SRCS   += dynArray.c

override CFLAGS += -DFIXED_POINT=16 -std=c99
override CFLAGS += -Wno-type-limits -Wno-sign-compare
override CFLAGS += -I$(APP_DIR)/kiss_fft

TOCK_BOARD = audio_module

# include makefile settings that are shared between applications
include ../../AppMakefile.mk

