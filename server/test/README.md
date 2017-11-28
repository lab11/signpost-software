Server-Side Tests
=================

The server side continuous integration testing runs a test setup of the 
signpost backend services in Travis.

It starts three MQTT brokers (one to simulate the lora-server, one internal
broker and one 'external' broker), then it starts all of the node services.

The test script publishes messages to the lora-server MQTT stream and
posts to the http-receiver, then checks the responses as the end of the processing
chain.
