#!/usr/bin/env node

//
// Forward mqtt packets from lora gateway to local mqtt stream
// Convert from hex string to bytes
//

var child_process = require('child_process');
var fs            = require('fs');
var ini           = require('ini');
var mqtt          = require('mqtt');
var request       = require('request');

// Read in the config file to get the parameters. If the parameters are not set
// or the file does not exist, we exit this program.
try {
    var signpost_config_file = fs.readFileSync('/etc/swarm-gateway/signpost-admin.conf', 'utf-8');
    var signpost_config = ini.parse(signpost_config_file);
    if (signpost_config.username == undefined || signpost_config.username == '' ||
        signpost_config.password == undefined || signpost_config.password == '') {
        throw new Exception('no settings');
    }
} catch (e) {console.log(e)
    console.log('Could not find /etc/swarm-gateway/signpost-admin.conf');
    process.exit(1);
}

try {
    var lora_config_file = fs.readFileSync('/etc/swarm-gateway/lora.conf', 'utf-8');
    var lora_config = ini.parse(lora_config_file);
    if (lora_config.host == undefined || lora_config.host == '') {
        throw new Exception('no settings');
    }
} catch (e) {console.log(e)
    console.log('Could not find /etc/swarm-gateway/lora.conf or lora not configured.');
    process.exit(1);
}

//Manage the LoRa node IDs
//We need to do this online to translate signpost MACs to devEUIs
//This requires logging in to the CMU API
var nodeDict = {};
var devDict = {};
function updateNodes() {
    console.log('updating node dict...');
    //first login
    var jwt = '';
    request({url: 'https://openchirp.io:8080/api/internal/login', 
            method: "POST",
            json: {username: lora_config.username, password: lora_config.password}}, 

        function(error, response, body) {
            try {
                jwt = body['jwt']; 
            } catch(e) {
                console.log(e);
            }

            //then update the node to devEUI mapping
            request({url: 'https://openchirp.io:8080/api/applications/5/nodes?limit=1000', 
                    method: "GET",
                    headers: {'grpc-metadata-authorization': jwt}}, 

                function(error, response, body) {
                    try {
                        nodelist = JSON.parse(body)['result'];
                    } catch(e) {
                        console.log(e);
                    }

                    //make a dictionary of each node
                    for (var i = 0; i < nodelist.length; i++) {
                        dev = nodelist[i];
                        nodeDict[dev['name'].toLowerCase()] = dev['devEUI'].toLowerCase();
                        devDict[dev['devEUI'].toLowerCase()] = dev['name'].toLowerCase();
                    }

                    console.log('node dict successfully updated');
                });
        });
}

updateNodes();
var loginInterval = setInterval(updateNodes, 3600000);


var mqtt_lora = mqtt.connect(lora_config.protocol + '://' + lora_config.host + ':' + lora_config.port, {username: lora_config.username, password: lora_config.password});
var mqtt_external = mqtt.connect('mqtt://localhost:8883',{username: signpost_config.username, password: signpost_config.password});

//Get messages from the external broker and parse them
mqtt_external.on('connect', function () {
    // Subscribe to all packets
    mqtt_external.subscribe('signpost/#');

});

var messageQueue = {};

function sendMessage(deviceID) {
    sendObj =  {
        confirmed: true,
        data: messageQueue[deviceID][0].data,
        devEUI: nodeDict.deviceID,
        fPort: 1,
        reference: deviceID,
    }

    mqtt_lora.publish('application/5/node/' + nodeDict[deviceID] + '/tx', json.stringify(sendObj));
}

// Callback for each packet
mqtt_external.on('message', function (topic, message) {
    //is the topic valid?
    var parts = topic.split('/');
    var node = '';
    var downtopic = '';
    if(!(parts[1] in nodeDict)) {
        //we don't know that signpost
        return;
    } else {
        node = parts[1];
    }

    if(!(parts[2].length < 12)) {
        //module name too long
        return;
    }
    
    var tlen = 0;
    for (var part in parts) {
        tlen += part.length;
    }
    tlen -= 20;

    if(!(tlen < 28)) {
        //topic name too long
        return;
    } else  {
        downtopic = topic.slice(20);
    }

    try {
        var json = JSON.parse(message.toString());
    } catch(e) {
        console.log('Invalid Message!');
        return;
    }

    if(json.data && !(json.receiver)) {
        console.log('Got downlink message on topic ' + topic);
        
        //this was published for downlink (i.e. not by our scripts)
        datstring = '';
        if(json.data.type) {
            if(json.data.type == "Buffer" && json.data.data) {
                //this is a buffer object that we should parse
                if(json.data.data.length < 96) {
                    datstring = json.data.data.toString('base64');
                }
            } else {
                //We don't know what to do with this
                console.log('Invalid downlink type! Dropping.');
                return;
            }
        } else {
            //try to interpret the data as base64
            try {
                testString = btoa(json.data);
            } catch(e) {
                console.log('Not properly formatted base64! Dropping.');
                return;
            }
            datstring = json.data
        }

        //this is a valid sized array
        //form and publish the lora request
        if(!(node in messageQueue)) {
            messageQueue[node] = [];
        }

        downobj = {
            deviceID: node,
            topic: downtopic,
            data: datstring,
        }

        messageQueue[node].push(downobj);

        //is this the only message in the queue? If so - send it
        if(messageQueue[node].length == 1) {
            sendMessage(node);
        }

    } else {
        if(json.data) {
            console.log('Got uplink message on topic ' + topic);
        } else {
            console.log('Invalid Message!');
        }
    }
});


//get acks from the lora stream
mqtt_lora.on('connect', function() {
    mqtt_lora.subscribe('application/5/node/+/ack');
});

mqtt_lora.on('message', function(topic, message) {
    //parse acks - send next packet
    json = JSON.parse(message.toString());
    devID = json.reference;
    messageQueue[devID].pop();
    
    if(messageQueue[devID].length > 0) {
        sendMessage(devID);
    }
});

