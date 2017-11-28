Server-Side Tests
=================

The server side continuous integration testing spins up mosquitto brokers
and the node backend services in travis, then publishes messages as
if it were the lora-server, and watches for correct responses.

Currently it only supports the lora-receiver.
