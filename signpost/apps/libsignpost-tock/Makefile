# Libsignpost makefile. Can build libsignpost standalone. Also included by
# application makefiles to ensure their libsignpost dependency is built

# Base folder definitions
TOCK_USERLAND_BASE_DIR ?= ../libtock-c
SIGNPOST_USERLAND_BASE_DIR ?= ..
LIBNAME := libsignpost-tock
LIBNAMETOCK := libsignpost
$(LIBNAME)_DIR=$(SIGNPOST_USERLAND_BASE_DIR)/$(LIBNAME)
$(LIBNAMETOCK)_DIR=$(SIGNPOST_USERLAND_BASE_DIR)/$(LIBNAMETOCK)

# Grab all source files
$(LIBNAME)_SRCS += $(wildcard $($(LIBNAME)_DIR)/*.s)
$(LIBNAME)_SRCS += $(wildcard $($(LIBNAME)_DIR)/*.c)
$(LIBNAME)_SRCS += $(wildcard $($(LIBNAME)_DIR)/*.cc)
$(LIBNAME)_SRCS += $(wildcard $($(LIBNAME)_DIR)/*.cpp)

# Grab all source files
$(LIBNAME)_SRCS += $(wildcard $($(LIBNAMETOCK)_DIR)/*.s)
$(LIBNAME)_SRCS += $(wildcard $($(LIBNAMETOCK)_DIR)/*.c)
$(LIBNAME)_SRCS += $(wildcard $($(LIBNAMETOCK)_DIR)/*.cc)
$(LIBNAME)_SRCS += $(wildcard $($(LIBNAMETOCK)_DIR)/*.cpp)

# External libraries
override CPPFLAGS += -I$(TOCK_USERLAND_BASE_DIR)/libtock
override CPPFLAGS += -I$(SIGNPOST_USERLAND_BASE_DIR)/support/fatfs
override CPPFLAGS += -I$(SIGNPOST_USERLAND_BASE_DIR)/support/mbedtls/mbedtls/include
include $(SIGNPOST_USERLAND_BASE_DIR)/support/erpc/AppLibERPC.mk

include $(TOCK_USERLAND_BASE_DIR)/TockLibrary.mk

