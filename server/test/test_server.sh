#!/usr/bin/env bash

bold=$(tput bold)
normal=$(tput sgr0)

black=$(tput setaf 0)
red=$(tput setaf 1)
green=$(tput setaf 2)
blue=$(tput setaf 4)

set -e

#start all the mosquitto brokers
mosquitto -c ./conf/mosquitto/internal.conf &
mosquitto -c ./conf/mosquitto/lora.conf &
mosquitto -c ./conf/mosquitto/external.conf &

#start all of the node services
node ../uplink/http-receiver/http-receiver.js &
node ../uplink/lora-receiver/lora-receiver.js ./conf/signpost/uplink/lora-receiver.conf &
node ../uplink/lora-receiver/metadata-tagger.js ./conf/signpost/uplink/metadata-tagger.conf &
node ../lab11/packet-parser/packet-parser.js ./conf/signpost/lab11/packet-parser.conf &

node ./test_server/test_server.js
