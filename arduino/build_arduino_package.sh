#!/bin/bash
CONFIG_PATH="config"
PACKAGE_PATH="packages/1.6.15"
BUILD_PATH="build"
PACKAGE_BUILD_PATH=${BUILD_PATH}/signpost/samd
LIBSIGNPOST_PATH="../apps/libsignpost"
LIBSIGNPOST_BUILD_PATH=${PACKAGE_BUILD_PATH}/libraries/Signpost
MBEDTLS_PATH="../apps/support/mbedtls/mbedtls"
#If developer flag is set, create hard links to files instead of copies
if [ "$1" = "-d" ]
then
	DEVELOPER="--link"
	echo "Buidling developer package"
else
	DEVELOPER=
	echo "Building consumer package"
fi

set -x
#Clear build directory

rm -rf ${BUILD_PATH}/*
#Create neccessary directories
mkdir -p ${PACKAGE_BUILD_PATH}

#Copy base samd Arduino hardware package
cp -r ${PACKAGE_PATH}/* ${PACKAGE_BUILD_PATH}/

#Copy config files
mkdir -p ${LIBSIGNPOST_BUILD_PATH}/src/
cp ${DEVELOPER} ${CONFIG_PATH}/platform.txt ${PACKAGE_BUILD_PATH}/
cp ${DEVELOPER} ${CONFIG_PATH}/boards.txt ${PACKAGE_BUILD_PATH}/
cp ${DEVELOPER} ${CONFIG_PATH}/programmers.txt ${PACKAGE_BUILD_PATH}/
cp ${DEVELOPER} ${CONFIG_PATH}/library.properties ${LIBSIGNPOST_BUILD_PATH}/

#Copy signpost files
cp ${DEVELOPER} ${LIBSIGNPOST_PATH}/signpost_api.* ${LIBSIGNPOST_BUILD_PATH}/src/
cp ${DEVELOPER} ${LIBSIGNPOST_PATH}/signbus_app_layer.* ${LIBSIGNPOST_BUILD_PATH}/src/
cp ${DEVELOPER} ${LIBSIGNPOST_PATH}/signbus_protocol_layer.* ${LIBSIGNPOST_BUILD_PATH}/src/
cp ${DEVELOPER} ${LIBSIGNPOST_PATH}/signbus_io_interface.* ${LIBSIGNPOST_BUILD_PATH}/src/
cp ${DEVELOPER} ${LIBSIGNPOST_PATH}/port_signpost.h ${LIBSIGNPOST_BUILD_PATH}/src/
cp ${DEVELOPER} ${LIBSIGNPOST_PATH}/port_signpost_arduino.cpp ${LIBSIGNPOST_BUILD_PATH}/src/
cp ${DEVELOPER} ${LIBSIGNPOST_PATH}/signpost_entropy.* ${LIBSIGNPOST_BUILD_PATH}/src/
cp ${DEVELOPER} ${LIBSIGNPOST_PATH}/CRC16.* ${LIBSIGNPOST_BUILD_PATH}/src/

#Copy mbedtls
mkdir ${LIBSIGNPOST_BUILD_PATH}/src/mbedtls
cp -r ${DEVELOPER} ${MBEDTLS_PATH}/library/ ${LIBSIGNPOST_BUILD_PATH}/src/mbedtls/
cp -r ${DEVELOPER} ${MBEDTLS_PATH}/include/ ${LIBSIGNPOST_BUILD_PATH}/src/mbedtls/
