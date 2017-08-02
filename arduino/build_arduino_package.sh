#!/bin/bash
CONFIG_PATH="config"
PACKAGE_PATH="packages/1.6.15"
BUILD_PATH="build"
PACKAGE_BUILD_PATH=${BUILD_PATH}/signpost/samd
LIBSIGNPOST_PATH="../apps/libsignpost"
LIBSIGNPOST_BUILD_PATH=${PACKAGE_BUILD_PATH}/libraries/Signpost/
MBEDTLS_PATH="../apps/support/mbedtls/mbedtls"

set -x
#Clear build directory
rm -r ${BUILD_PATH}/*
#Create neccessary directories
mkdir ${BUILD_PATH}/signpost
mkdir ${BUILD_PATH}/signpost/samd

#Copy base samd Arduino hardware package
cp -r ${PACKAGE_PATH}/* ${BUILD_PATH}

#Copy config files
cp ${CONFIG_PATH}/platform.txt ${PACKAGE_BUILD_PATH}
cp ${CONFIG_PATH}/boards.txt ${PACKAGE_BUILD_PATH}
cp ${CONFIG_PATH}/programmers.txt ${PACKAGE_BUILD_PATH}
cp ${CONFIG_PATH}/library.properties ${LIBSIGNPOST_BUILD_PATH}

#Copy signpost files
cp ${LIBSIGNPOST_PATH}/signpost_api.* ${LIBSIGNPOST_BUILD_PATH}/src
cp ${LIBSIGNPOST_PATH}/signpost_app_layer.* ${LIBSIGNPOST_BUILD_PATH}/src
cp ${LIBSIGNPOST_PATH}/signpost_protocol_layer.* ${LIBSIGNPOST_BUILD_PATH}/src
cp ${LIBSIGNPOST_PATH}/signpost_io_interface.* ${LIBSIGNPOST_BUILD_PATH}/src
cp ${LIBSIGNPOST_PATH}/port_signpost.h ${LIBSIGNPOST_BUILD_PATH}/src
cp ${LIBSIGNPOST_PATH}/port_signpost_arduino.cpp ${LIBSIGNPOST_BUILD_PATH}/src
cp ${LIBSIGNPOST_PATH}/CRC16.* ${LIBSIGNPOST_BUILD_PATH}/src

#Copy mbedtls
cp -r ${MBEDTLS_PATH} ${LIBSIGNPOST_BUILD_PATH}/src
