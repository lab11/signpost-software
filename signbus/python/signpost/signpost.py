#!/usr/bin/env python3

import logging
log = logging.getLogger(__name__)

import enum

from . import signbus

class ModuleAddress(enum.IntEnum):
    Controller = 0x20,
    Storage = 0x21,
    Radio = 0x22,

class FrameType(enum.IntEnum):
    Notification = 0
    Command = 1
    Response = 2
    Error = 3

class ApiType(enum.IntEnum):
    Initialization = 1
    Storage = 2
    Networking = 3
    Processing = 4
    Energy = 5
    TimeLocation = 6
    Edison = 7

class EdisonApiMessageType(enum.IntEnum):
    ReadHandle = 0
    ReadRPC = 1

class ProcessingMessageType(enum.IntEnum):
    ProcessingEdisonReadMessage = 3,
    ProcessingEdisonResponseMessage = 4,

class EdisonApiClient():
    DEFAULT_EDISON_MODULE_ADDRESS = 0x40

    def __init__(self, *,
            signbus_instance=None):

        if signbus_instance is None:
            # Create an instance
            signbus_instance = signbus.Signbus(source_address=EdisonApiClient.DEFAULT_EDISON_MODULE_ADDRESS)
        self._signbus = signbus_instance

    def send_read_handle(self, *,
            dest,
            handle):
        '''handle should be an array of 8 bytes'''
        self._signbus.send(
                dest=dest,
                frame_type=FrameType.Notification, # XXX: notifcation b/c no reply
                api_type=ApiType.Edison,
                message_type=EdisonApiMessageType.ReadHandle,
                payload=handle,
                )

    def read_RPC(self):

        #send the command that lets the storage master know we are about
        #to read the RPC so it can prep its buffer
        self._signbus.send(
                dest=ModuleAddress.Storage,
                frame_type=FrameType.Notification, #no reply
                api_type=ApiType.Processing,
                message_type=ProcessingApiMessageType.ProcessingEdisonReadMessage,
                payload=None)

        #perform the read, return the results
        return self._signbus._net.read(ModuleAddress.Storage, 255)


