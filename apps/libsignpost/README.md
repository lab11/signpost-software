Signpost Library Code
=====================

This folder contains libraries that are portable across
platforms. This primarily includes the core signpost API, but also has some
generic libraries (such as CRC).

To port the signpost API to a new platform:
 - Create a new directory apps/libsignpost-platform
 - Implement port_signpost.h in this directory
 - Add a makefile for your platform that builds the signpost port implementation
 with the core signpost libraries. Apps that use the new platform will use this makefile.
