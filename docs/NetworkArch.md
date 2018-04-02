Network Architecture
====================

Due to the mix of radios and radio stacks on Signposts, the networking stack
is rather convoluted. We created an architecture that tries to utilize
the variety of radios to provide improved latency and quality of service
without over-complicating the API.

## Uplink Data

The Signpost API currently provices a [pub/sub interface](https://github.com/lab11/signpost-software/blob/master/docs/ApiGuide.md#networking)
and 

The publish method receives data packets from signpost modules, queues that
data, then opportunistically uses either the cellular radio or
the lora radio to push these packets to the signpost backend server. Once
they reach the server, they are deduplicated, tagged with time, location
and device ID metadata, and published to an MQTT broker (which you can access
with a username and password). A full picture of the networking
stack is shown below.

<img src="https://raw.githubusercontent.com/lab11/signpost-software/master/docs/img/uplink_network_arch.jpg" width="35%" />

## Downlink Data

Downlink can be accomplished by publishing bytes to the appropriate MQTT
topic. This data is then queued for each signpost, which will eventually
either pull data from the queue over the lora network or the cellular
radio, then dispatch this data to the appropriate module based on the
topic.

The downlink architecture is shown below:

<img src="https://raw.githubusercontent.com/lab11/signpost-software/master/docs/img/downlink_network_arch.jpg" width="35%" />


