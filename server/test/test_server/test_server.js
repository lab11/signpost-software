#!/usr/bin/env node

//
// Forward mqtt packets from lora gateway to local mqtt stream
// Convert from hex string to bytes
//

var child_process = require('child_process');
var fs            = require('fs');
var ini           = require('ini');
var mqtt          = require('mqtt');

try {
    var config_file = fs.readFileSync('./test_server.conf', 'utf-8');
    var config = ini.parse(config_file);
} catch (e) {console.log(e)
    console.log('No configuration file found.'); 
    process.exit(1);
}

//Data collected by subscribing to lora to get the input buffers
//and subscribing to gateway-data to get the output buffers

var seq = 0;
function build_lora_packet_buffer(addr, topic, data) {
    addrBuf = Buffer.from(addr);
    if(addrBuf.length != 6) {
        console.log('Invalid Address');
        return;
    }

    topicBuf = Buffer.from(topic);
    pBuf = Buffer.alloc(addrBuf.length + topicBuf.length + data.length + 2);
    addrBuf.copy(pBuf);
    pBuf[6] = seq;
    seq += 1;

    pBuf[7] = topicBuf.length;
    topicBuf.copy(pBuf, 8);
    pBuf[topicBuf.length + 6 + 2] = data.length;
    data.copy(pBuf, topicBuf.length + 6 + 3);
    return pBuf;
}



// Setup the two MQTT brokers
var mqtt_lora = mqtt.connect('mqtt://localhost:' + config.outgoing_port, {username: config.outgoing_username, password: config.outgoing_password});
var mqtt_internal = mqtt.connect('mqtt://localhost:' + config.incoming_port);

// Listen for packets on gateway-data
mqtt_internal.on('connect', function () {
    // Subscribe to all packets
    mqtt_internal.subscribe('gateway-data');
    
    //send the first buffer to the lora topic
});

// Callback for each packet
mqtt_internal.on('message', function (topic, message) {
    
    //check that the message is what we expect
    
    //send the next buffer to the lora topic

});

