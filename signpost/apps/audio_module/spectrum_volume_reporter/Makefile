# makefile for user application

# the current directory
APP_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# files needed for this code
C_SRCS   := $(wildcard *.c)

TOCK_BOARD = audio_module

# include makefile settings that are shared between applications
include ../../AppMakefileTock.mk

