# makefile for user application

# the current directory
APP_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

APP_SRCS += md.c
APP_SRCS += md_wrap.c
APP_SRCS += sha256.c
APP_SRCS += rsa.c
APP_SRCS += pk.c
APP_SRCS += pkparse.c
APP_SRCS += pk_wrap.c
APP_SRCS += bignum.c
APP_SRCS += oid.c
APP_SRCS += pkcs12.c
APP_SRCS += cipher.c
APP_SRCS += cipher_wrap.c
APP_SRCS += pem.c
APP_SRCS += base64.c
APP_SRCS += aes.c
APP_SRCS += asn1parse.c
APP_SRCS += pkcs5.c

# files needed for this code

SOURCE_PATHS += .
SOURCE_PATHS += ../../../libs/mbedtls/library/

VPATH=$(SOURCE_PATHS)

C_SRCS   := $(wildcard *.c)
C_SRCS   += $(notdir $(APP_SRCS))

# include makefile settings that are shared between applications
include ../../../AppMakefileTock.mk

