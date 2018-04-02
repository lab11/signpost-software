Energy Allocation Policy
=======================

For fair resource sharing, signpost monitors all energy harvested and consumed
by modules. If a module uses over its fair share, the signpost control module
will turn off that module.

A module's fair share is determined by equally splitting all energy that is 
harvested by the solar panel between modules. Each module has a "virtual battery"
where this harvested energy is accumulated, and when modules use energy, this
energy is subtracted from their virtual battery. If a module's virtual battery
is empty, it is turned off. If it is full no more energy can be stored and
the excess is distributed equally to the other modules. Energy used by other
modules on a module's behalf (most notably by the radio) is also reported
and deducted from a modules virtual battery. Currently each modules virtual battery
is set 10\% of the physical battery on the platform (about 10Wh per module).

Modules can query the amount of energy they have remaining using the [energy
API](https://github.com/lab11/signpost-software/blob/dmaster/docs/ApiGuide.md#energy). 
Energy statistics are also reported to the cloud by the controller as documented
in the [data schemas page](https://github.com/lab11/signpost-software/blob/master/docs/DataSchemas.md).

This is a simple energy policy, and it has a lot of room for improvement! If you
would like to test different policies, specifically those that include 
module/data priority or energy contracts between modules please get in touch with us! 
