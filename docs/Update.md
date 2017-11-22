Updating a Signpost
===================

Robust update is fundamental to a city-scale deployment, and it is the
point in the system that is most vulnerable to failure and attack. 

Currently we have what we believe to be a working but insufficient long-term
story for updating application, which we lay out below. 

##Current Update Architecture

The update procedure can fundamentally be broken down into several steps:

1. Signal Signpost module that it should update
2. Fetch the update and save it to flash
3. Copy and execute new code

In the Signpost architecture (1) is accomplished by sending an update
message containing all relevant update information to signpost/mac\_address/module\_name/update.
In (2) the update is fetched using the cellular radio, and transferred piece
by piece to the sensor module. (3) The update is (potentially) copied
to a new location and executed. This process will almost always require
a bootloader, which can either execute at step (2) or step (3). We believe
that running the bootloader in step (2) will be more robust, because
in the case of failure another update could be transferred to the module.

We plan to provide a small bootloader that can execute this
task for our supported platforms. This will in practice sit as a Tock application
and be a more traditional bootloader for platforms such as Mbed and Arduino. 
