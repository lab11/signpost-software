#!/usr/bin/env node

//
// Forward mqtt packets from lora gateway to local mqtt stream
// Convert from hex string to bytes
//

var child_process = require('child_process');
var fs            = require('fs');
var ini           = require('ini');
var mqtt          = require('mqtt');
var addr          = require('os').networkInterfaces();

//check to see if there was a conf file passed in
var lora_conf_file_location = '';
if(process.argv.length < 4) {
    lora_conf_file_location = '/etc/signpost/lora.conf';
} else {
    lora_conf_file_location = process.argv[2];
}

var mqtt_conf_file_location = '';
if(process.argv.length < 4) {
    mqtt_conf_file_location = '/etc/signpost/mqtt.conf';
} else {
    mqtt_conf_file_location = process.argv[3];
}

try {
    var lora_config_file = fs.readFileSync(lora_conf_file_location, 'utf-8');
    var lora_config = ini.parse(lora_config_file);
    if(lora_config.port == undefined ||
        lora_config.protocol == undefined ||
        lora_config.host == undefined ||
        lora_config.username == undefined ||
        lora_config.password == undefined) {
        console.log('Invalid configuration file. See signpost-software/server/test/conf/signpost for valid configuration files');
        process.exit(1);
    }
} catch (e) {console.log(e)
    console.log('No configuration file found. Either pass a configuration path or place a file at /etc/signpost/uplink/lora-receiver.conf.');
    process.exit(1);
}

try {
    var mqtt_config_file = fs.readFileSync(mqtt_conf_file_location, 'utf-8');
    var mqtt_config = ini.parse(mqtt_config_file);
    if(mqtt_config.internal_port == undefined) {
        console.log('Invalid configuration file. See signpost-software/server/test/conf/signpost for valid configuration files');
        process.exit(1);
    }
} catch (e) {console.log(e)
    console.log('No configuration file found. Either pass a configuration path or place a file at /etc/signpost/uplink/lora-receiver.conf.');
    process.exit(1);
}

function pad (s, len) {
    for (var i=s.length; i<len; i++) {
        s = '0' + s;
    }
    return s;
}

function parse (buf) {
    // Strip out address
    var addr = '';
    for (var i=0; i<6; i++) {
        addr += pad(buf[i].toString(16), 2);
    }

    if(typeof parse.last_sequence_numbers == 'undefined') {
        parse.last_sequence_numbers = {};
    }
    
    var first = false;
    if(typeof parse.last_sequence_numbers[addr] == 'undefined') {
        first = true;
        parse.last_sequence_numbers[addr] = 0;
    }
    
    var sequence_number = buf.readUInt8(6);
    
    if(parse.last_sequence_numbers[addr] == sequence_number && !first) {
        return {};
    } else {
        parse.last_sequence_numbers[addr] = sequence_number;
    }
    
    var done = false;
    var index = 7;
    var pcount = 0;
    var ret = {};
    while(done == false) {
        var tlen = buf.readUInt8(index);
        index += 1;
        var topic = buf.toString('utf-8',index, index+tlen);
        index += tlen;
        var dlen = buf.readUInt8(index);
        index += 1;
        var data = buf.slice(index,index+dlen);
        index += dlen;
        pcount += 1;
        ret[pcount.toString()] = {}; 
        ret[pcount.toString()].topic = topic; 
        ret[pcount.toString()].topublish = {};
        ret[pcount.toString()].topublish.data = data; 
        ret[pcount.toString()].topublish.receiver  = 'lora'; 
        ret[pcount.toString()].topublish.received_time = new Date().toISOString();
        ret[pcount.toString()].topublish.device_id = addr;
        ret[pcount.toString()].topublish.sequence_number = sequence_number;

        if(buf.length <= index) {
            done = true;
        }
    }

    return ret;
}    

var mqtt_client_lora = mqtt.connect(lora_config.protocol + '://' + lora_config.host + ':' + lora_config.port, {username: lora_config.username, password: lora_config.password});
var mqtt_client_outgoing = mqtt.connect('mqtt://localhost:' + mqtt_config.internal_port);
mqtt_client_lora.on('connect', function () {
    // Subscribe to all packets
    mqtt_client_lora.subscribe('application/5/node/+/rx');

    // Callback for each packet
    mqtt_client_lora.on('message', function (topic, message) {
        var json = JSON.parse(message.toString());
        try {
            if(json.data) {
                buf = Buffer.from(json.data, 'base64');
                console.log(buf.toString('hex'));
                if(buf.length > 6) {
                    var pkt = parse(buf);
                }

                //pkt returns an array of things to publish
                for(var key in pkt) {
                    console.log("Publishing to " + 'signpost-preproc/' + pkt[key].topic);
                    mqtt_client_outgoing.publish('signpost-preproc/' + pkt[key].topic, JSON.stringify(pkt[key].topublish));
                }
            }
        } catch (e) {
            console.log(e)
        }

    });
});
