Server-Side Software for Signpost
=================================

This folder contains software for the signpost backend. This includes
code responsible for uplink, metadata tagging, downlink, and lab11 specific
parsing scripts. It is organized as follows:

##Uplink
These scripts receive signpost packets over HTTP post and from the Lora server,
then parse those packets, tag them with metadata, and push them to the
external MQTT broker.

##Downlink
The code that listens on the MQTT stream for downlink messages, queues
these messages, then sends them down to individual signposts.

##Lab11
Lab11-specific parsing scripts. These parse the buffers posted to the external
MQTT broker into typed, usable data, which is eventually posted to both our
InfluxDB and TimescaleDB backends. 
