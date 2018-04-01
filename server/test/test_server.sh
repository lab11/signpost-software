#!/usr/bin/env bash

bold=$(tput bold)
normal=$(tput sgr0)

black=$(tput setaf 0)
red=$(tput setaf 1)
green=$(tput setaf 2)
blue=$(tput setaf 4)

set -e

#start all the mosquitto brokers
mosquitto -c ./conf/mosquitto/internal_conf.d/internal.conf &
M1_PID=$!
mosquitto -c ./conf/mosquitto/lora_conf.d/lora.conf &
M2_PID=$!
mosquitto -c ./conf/mosquitto/external_conf.d/external.conf &
M3_PID=$!

#start all of the node services
node ../uplink/http-receiver/http-receiver.js ./conf/signpost/http.conf ./conf/signpost/mqtt.conf &
N1_PID=$!
node ../uplink/lora-receiver/lora-receiver.js ./conf/signpost/lora.conf ./conf/signpost/mqtt.conf &
N2_PID=$!
node ../uplink/metadata-tagger/metadata-tagger.js ./conf/signpost/mqtt.conf &
N3_PID=$!
node ../lab11/packet-parser/packet-parser.js ./conf/signpost/lab11/packet-parser.conf &
N4_PID=$!

node ./test-server/test-server.js

kill -9 $M1_PID
kill -9 $M2_PID
kill -9 $M3_PID
kill -9 $N1_PID
kill -9 $N2_PID
kill -9 $N3_PID
kill -9 $N4_PID
