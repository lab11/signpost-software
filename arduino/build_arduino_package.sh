#!/bin/bash
#Syntax: ./build_arduino_package [-d] SKETCHBOOK_PATH
#Adding -d builds a developer package by creating symlinks to the original files instead of copies
#SKETCHBOOK_PATH is the path to the sketchbook for your Arduino IDE installation
#On Linux this is usually ~/Arduino/
#On Windows this is usually ~/Documents/Arduino/
#NOTE: Script will not execute properly if any file paths have any spaces in them

#Set paths to source files
CONFIG_PATH="config"
PACKAGE_PATH="packages/1.6.15"
LIBSIGNPOST_PATH="../apps/libsignpost"
MBEDTLS_PATH="../apps/support/mbedtls/mbedtls"

#Process command line arguments
case "$#" in
	"1")
		if [ ! -d "$1" ] ; then
			echo "${1} is a not a directory, exiting..."
			exit 1
		fi
		SKETCHBOOK_PATH="$1"
		DEVELOPER=
		echo "Building consumer package"
		;;
	"2")
		#Check to see which argument is the sketchbook path
		#If neither of the arguments are directories, exit
		#If neither of the arguments are "-d", exit
		if [ -d "$1" ] && [ "$2" = "-d" ] ; then
			SKETCHBOOK_PATH="$1"
		elif [ -d "$2" ] && [ "$1" = "-d" ] ; then
			SKETCHBOOK_PATH="$2"
		else
			if [ ! -d "$1" ] && [ ! -d "$2" ] ; then
				echo "Neither input argument is a directory, exiting..."
			else
				echo "Invalid syntax, exiting..."
			fi
			exit 1
		fi
		DEVELOPER="-sR"
		echo "Building developer package"
		;;
	*)
		echo "Invalid number of arguments, exiting..."
		exit 1
		;;
esac
#If last character of SKETCHBOOK_PATH is not a '/', then add one
if [ ! "${SKETCHBOOK_PATH: -1}" = "/" ] ; then
	SKETCHBOOK_PATH=${SKETCHBOOK_PATH}/
fi

#Set build paths
PACKAGE_BUILD_PATH=${SKETCHBOOK_PATH}hardware/signpost/samd
LIBSIGNPOST_BUILD_PATH=${PACKAGE_BUILD_PATH}/libraries/Signpost

#Echo all commands
set -x

#Clear old package
rm -rf ${SKETCHBOOK_PATH}hardware/signpost/

#Create neccessary directories
mkdir -p ${PACKAGE_BUILD_PATH}

#Copy base samd Arduino hardware package
cp -r ${PACKAGE_PATH}/* ${PACKAGE_BUILD_PATH}/

#Copy/link config files
mkdir -p ${LIBSIGNPOST_BUILD_PATH}/src/
cp ${DEVELOPER} ${PWD}/${CONFIG_PATH}/platform.txt ${PACKAGE_BUILD_PATH}/
cp ${DEVELOPER} ${PWD}/${CONFIG_PATH}/boards.txt ${PACKAGE_BUILD_PATH}/
cp ${DEVELOPER} ${PWD}/${CONFIG_PATH}/programmers.txt ${PACKAGE_BUILD_PATH}/
cp ${DEVELOPER} ${PWD}/${CONFIG_PATH}/library.properties ${LIBSIGNPOST_BUILD_PATH}/

#Copy/link signpost files
cp ${DEVELOPER} ${PWD}/${LIBSIGNPOST_PATH}/signpost_api.* ${LIBSIGNPOST_BUILD_PATH}/src/
cp ${DEVELOPER} ${PWD}/${LIBSIGNPOST_PATH}/signbus_app_layer.* ${LIBSIGNPOST_BUILD_PATH}/src/
cp ${DEVELOPER} ${PWD}/${LIBSIGNPOST_PATH}/signbus_protocol_layer.* ${LIBSIGNPOST_BUILD_PATH}/src/
cp ${DEVELOPER} ${PWD}/${LIBSIGNPOST_PATH}/signbus_io_interface.* ${LIBSIGNPOST_BUILD_PATH}/src/
cp ${DEVELOPER} ${PWD}/${LIBSIGNPOST_PATH}/port_signpost.h ${LIBSIGNPOST_BUILD_PATH}/src/
cp ${DEVELOPER} ${PWD}/${LIBSIGNPOST_PATH}/port_signpost_arduino.cpp ${LIBSIGNPOST_BUILD_PATH}/src/
cp ${DEVELOPER} ${PWD}/${LIBSIGNPOST_PATH}/signpost_entropy.* ${LIBSIGNPOST_BUILD_PATH}/src/
cp ${DEVELOPER} ${PWD}/${LIBSIGNPOST_PATH}/CRC16.* ${LIBSIGNPOST_BUILD_PATH}/src/

#Copy/link mbedtls files
mkdir ${LIBSIGNPOST_BUILD_PATH}/src/mbedtls
cp -r ${DEVELOPER} ${PWD}/${MBEDTLS_PATH}/library/ ${LIBSIGNPOST_BUILD_PATH}/src/mbedtls/
cp -r ${DEVELOPER} ${PWD}/${MBEDTLS_PATH}/include/ ${LIBSIGNPOST_BUILD_PATH}/src/mbedtls/
