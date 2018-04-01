#!/usr/bin/env node

//
// Forward mqtt packets from lora gateway to local mqtt stream
// Convert from hex string to bytes
//

var child_process = require('child_process');
var fs            = require('fs');
var ini           = require('ini');
var mqtt          = require('mqtt');
var http          = require('http');

try {
    var config_file = fs.readFileSync('./test-server/test-server.conf', 'utf-8');
    var config = ini.parse(config_file);
} catch (e) {console.log(e)
    console.log('No configuration file found.'); 
    process.exit(1);
}

//Data collected by subscribing to lora to get the input buffers
//and subscribing to gateway-data to get the output buffers

var seq = 0;
function build_lora_packet_buffer(addr, topic, data) {
    addrBuf = Buffer.from(addr, 'hex');
    if(addrBuf.length != 6) {
        console.log('Invalid Address');
        console.log(addrBuf.length);
        return;
    }

    topicBuf = Buffer.from(topic);
    pBuf = Buffer.alloc(addrBuf.length + topicBuf.length + data.length + 3);
    addrBuf.copy(pBuf);
    pBuf[6] = seq;
    seq += 1;

    pBuf[7] = topicBuf.length;
    topicBuf.copy(pBuf, 8);
    pBuf[topicBuf.length + 6 + 2] = data.length;
    data.copy(pBuf, topicBuf.length + 6 + 3);
    return pBuf;
}

function build_http_packet_buffer(addr, dlist) {
    addrBuf = Buffer.from(addr, 'hex');
    if(addrBuf.length != 6) {
        console.log('Invalid Address');
        console.log(addrBuf.length);
        return;
    }
    
    var len = 7;
    for (var i = 0; i < dlist.length; i++) {
        len += 4;
        len += dlist[i].topic.length;
        len += dlist[i].data.length;
    }
    
    pBuf = Buffer.alloc(len);
    addrBuf.copy(pBuf);
    pBuf[6] = seq;
    seq += 1;
    var index = 7;
    for (var i = 0; i < dlist.length; i++) {
        topicBuf = Buffer.from(dlist[i].topic);
        dataBuf = Buffer.from(dlist[i].data);
        var tlen = topicBuf.length + dataBuf.length + 2;
        pBuf.writeUInt16BE(tlen,index);
        index += 2;
        pBuf[index] = topicBuf.length;
        index += 1;
        topicBuf.copy(pBuf, index);
        index += topicBuf.length;
        pBuf[index] = dataBuf.length;
        index += 1;
        dataBuf.copy(pBuf, index);
        index += dataBuf.length;
    }

    return pBuf;
}

var testQueue = [];
var answerQueue = [];
var httpList = [];

function build_test_queue() {
    energy = Buffer.alloc(48);
    energy.writeUInt8(0x01, 0);
    energy.writeUInt16BE(12000, 1);
    energy.writeInt32BE(-20000, 3);
    energy.writeUInt16BE(19000, 7);
    energy.writeInt32BE(200000, 9);
    energy.writeUInt8(79, 13);
    energy.writeUInt16BE(1200, 14);
    energy.writeUInt16BE(9000, 16);
    energy.writeUInt16BE(10, 18);
    energy.writeUInt16BE(20, 20);
    energy.writeUInt16BE(30, 22);
    energy.writeUInt16BE(40, 24);
    energy.writeUInt16BE(0, 26);
    energy.writeUInt16BE(50, 28);
    energy.writeUInt16BE(60, 30);
    energy.writeUInt16BE(70, 32);
    energy.writeUInt16BE(80, 34);
    energy.writeUInt16BE(90, 36);
    energy.writeUInt16BE(100, 38);
    energy.writeUInt16BE(110, 40);
    energy.writeUInt16BE(120, 42);
    energy.writeUInt16BE(130, 44);
    energy.writeUInt16BE(140, 46);

    testQueue.push(build_lora_packet_buffer('c098e5120000', 'lab11/energy', energy));
    testQueue.push(build_lora_packet_buffer('c098e5120000', 'signpost/control/energy', energy));
    httpList.push({'topic': 'lab11/energy', 'data': energy});
    httpList.push({'topic': 'signpost/control/energy', 'data': energy});

    energy_answer =  {
                    device: "signpost_energy",
                    battery_voltage_mV: 12000,
                    battery_current_uA: -20000,
                    solar_voltage_mV: 19000,
                    solar_current_uA: 200000,
                    battery_capacity_percent_remaining: 79,
                    battery_capacity_remaining_mAh: 1200,
                    battery_capacity_full_mAh: 9000,
                    controller_energy_remaining_mWh: 40,
                    module0_energy_remaining_mWh: 10,
                    module1_energy_remaining_mWh: 20,
                    module2_energy_remaining_mWh: 30,
                    module5_energy_remaining_mWh: 50,
                    module6_energy_remaining_mWh: 60,
                    module7_energy_remaining_mWh: 70,
                    controller_energy_average_mW: 110,
                    module0_energy_average_mW: 80,
                    module1_energy_average_mW: 90,
                    module2_energy_average_mW: 100,
                    module5_energy_average_mW: 120,
                    module6_energy_average_mW: 130,
                    module7_energy_average_mW: 140,
                }
 
    gps = Buffer.alloc(17);
    gps.writeUInt8(0x01, 0);
    gps.writeUInt8(10, 1);
    gps.writeUInt8(8, 2);
    gps.writeUInt8(17, 3);
    gps.writeUInt8(12, 4);
    gps.writeUInt8(00, 5);
    gps.writeUInt8(00, 6);
    gps.writeInt32BE(37871600, 7);
    gps.writeInt32BE(-122272700, 11);
    gps.writeUInt8(3, 15);
    gps.writeUInt8(10, 16);
    testQueue.push(build_lora_packet_buffer('c098e5120000', 'lab11/gps', gps));
    testQueue.push(build_lora_packet_buffer('c098e5120000', 'signpost/control/gps', gps));
    httpList.push({'topic': 'lab11/gps', 'data': gps});
    httpList.push({'topic': 'signpost/control/gps', 'data': gps});

    gps_answer =  {
            device: 'signpost_gps',
            latitude: 37.8716,
            latitude_direction: 'N',
            longitude: 122.2727,
            longitude_direction: 'W',
            timestamp: new Date(Date.UTC(2017, 8-1, 10, 12, 00, 00)).toISOString(),
            satellite_count: 10,
        }


    ambient = Buffer.alloc(10);
    ambient.writeUInt8(0x01, 0);
    ambient.writeInt16BE(2500, 1);
    ambient.writeInt16BE(4500, 3);
    ambient.writeInt16BE(1000, 5);
    ambient.writeUInt8(0x0F, 7);
    ambient.writeUInt8(0x76, 8);
    ambient.writeUInt8(0x02, 9);

    testQueue.push(build_lora_packet_buffer('c098e5120000', 'lab11/ambient', ambient));
    testQueue.push(build_lora_packet_buffer('c098e5120000', 'lab11/ambient/tphl', ambient));
    httpList.push({'topic': 'lab11/ambient', 'data': ambient});
    httpList.push({'topic': 'lab11/ambient/tphl', 'data': ambient});

    ambient_answer =  {
                device: 'signpost_ambient',
                temperature_c: 25.0,
                humidity: 45.0,
                light_lux: 1000,
                pressure_pascals: 101325,
            }


    audio = Buffer.alloc(75);
    audio.writeUInt8(0x03, 0);
    audio.writeUInt32BE(1511851693, 1);
    for(var i = 0; i < 70; i++) {
        audio.writeUInt8(125, i+5);
    }
    testQueue.push(build_lora_packet_buffer('c098e5120000', 'lab11/audio', audio));
    testQueue.push(build_lora_packet_buffer('c098e5120000', 'lab11/audio/spectrum', audio));
    httpList.push({'topic': 'lab11/audio', 'data': audio});
    httpList.push({'topic': 'lab11/audio/spectrum', 'data': audio});

    audio_answer = {
            device: 'signpost_audio_frequency',
             "63Hz": 62.5,
            '160Hz': 62.5,
            '400Hz': 62.5,
            '1000Hz': 62.5,
            '2500Hz': 62.5,
            '6250Hz': 62.5,
            '16000Hz': 62.5,
    }

    

    radar = Buffer.alloc(25);
    radar.writeUInt8(0x02, 0);
    radar.writeUInt32BE(1511851693, 1);
    for(var i = 0; i < 20; i++) {
        radar.writeUInt8(125, i+5);
    }
    testQueue.push(build_lora_packet_buffer('c098e5120000', 'lab11/radar', radar));
    testQueue.push(build_lora_packet_buffer('c098e5120000', 'lab11/radar/motion', radar));
    httpList.push({'topic': 'lab11/radar', 'data': radar});
    httpList.push({'topic': 'lab11/radar/motion', 'data': radar});

    radar_answer = {
                    device: 'signpost_microwave_radar',
                     "motion_index": 125,
    }

    
 
    spectrum = Buffer.alloc(81);
    spectrum.writeUInt8(0x01, 0);
    for (var i = 0; i < 80; i++) {
        spectrum.writeInt8(-50, 1+i);
    }
 
    spectrum2 = Buffer.alloc(80);
    for (var i = 0; i < 80; i++) {
        spectrum2.writeInt8(-50, i);
    }

    testQueue.push(build_lora_packet_buffer('c098e5120000', 'lab11/spectrum', spectrum));
    testQueue.push(build_lora_packet_buffer('c098e5120000', 'lab11/spectrum/ws_max', spectrum2));
    httpList.push({'topic': 'lab11/spectrum', 'data': spectrum});
    httpList.push({'topic': 'lab11/spectrum/ws_max', 'data': spectrum2});

    spectrum_answer = {};
    spectrum_answer['device'] = 'signpost_rf_spectrum_max';
    for (var i = 0; i < 80; i++) {
            var lowend = 470+i*6;
            var highend = 470+6+i*6;
            var fullstr = lowend.toString()+"MHz"+"-"+highend.toString()+"MHz"+"_"+"max";
            spectrum_answer[fullstr] = -50;
    }


    answerQueue.push(energy_answer);
    answerQueue.push(energy_answer);
    answerQueue.push(gps_answer);
    answerQueue.push(gps_answer);
    answerQueue.push(ambient_answer);
    answerQueue.push(ambient_answer);
    for(var i = 0; i < 10; i++) {
        answerQueue.push(audio_answer);
    }
    for(var i = 0; i < 10; i++) {
        answerQueue.push(audio_answer);
    }
    for(var i = 0; i < 20; i++) {
        answerQueue.push(radar_answer);
    }
    for(var i = 0; i < 20; i++) {
        answerQueue.push(radar_answer);
    }
    answerQueue.push(spectrum_answer);
    answerQueue.push(spectrum_answer);

    //now add the http part of the answer queue
    answerQueue.push(energy_answer);
    answerQueue.push(energy_answer);
    answerQueue.push(gps_answer);
    answerQueue.push(gps_answer);
    answerQueue.push(ambient_answer);
    answerQueue.push(ambient_answer);
    for(var i = 0; i < 10; i++) {
        answerQueue.push(audio_answer);
    }
    for(var i = 0; i < 10; i++) {
        answerQueue.push(audio_answer);
    }
    for(var i = 0; i < 20; i++) {
        answerQueue.push(radar_answer);
    }
    for(var i = 0; i < 20; i++) {
        answerQueue.push(radar_answer);
    }
    answerQueue.push(spectrum_answer);
    answerQueue.push(spectrum_answer);
}



// Setup the two MQTT brokers
var mqtt_lora = mqtt.connect('mqtt://localhost:' + config.outgoing_port, {username: config.outgoing_username, password: config.outgoing_password});
var mqtt_internal = mqtt.connect('mqtt://localhost:' + config.incoming_port);

// Listen for packets on gateway-data
mqtt_internal.on('connect', function () {
    // Subscribe to all packets
    mqtt_internal.subscribe('gateway-data');

    build_test_queue();
    
    //send the first buffer to the lora topic
    var pubInt = setInterval( function() {
        if(testQueue.length == 0) {
            //stop the mqtt messages
            clearInterval(pubInt);

            //set the http message
            console.log('Posting to http receiver...');
            var buf = build_http_packet_buffer('c098e5120000',httpList);
            /*request.post( {
                method: 'POST',
                uri: 'http://localhost:8080/signpost',
                body: buf,
                encoding: null});*/

            var options = {
                host: 'localhost',
                port: 8080,
                path: '/signpost',
                method: 'POST',
                headers: {
                    'Content-Type': 'application/octet-stream',
                    'Content-Length': buf.length,
                }
            }

            var req = http.request(options, function(res) {

            });

            req.write(buf);
            req.end();
            
        } else {
            buf = testQueue.shift();
            mqtt_lora.publish("application/5/node/0/rx", JSON.stringify({"data": buf.toString('base64')}));
        }
    }, 2000);
});

// Callback for each packet
mqtt_internal.on('message', function (topic, message) {
    //check that the message is what we expect
    obj = JSON.parse(message);
    truObj = answerQueue.shift();
    for (var key in truObj) {
        if(key in obj) {
            if(obj[key] != truObj[key]) {
                console.log('Failed value comparison of key: ' + key);
                console.log(obj);
                console.log(truObj);
                process.exit(1);
            }
        } else {
            console.log('Failed field comparison');
            console.log(obj);
            console.log(truObj);
            process.exit(1);
        }
    }

    if(answerQueue.length == 0) {
        console.log("All tests passed");
        process.exit(0);
    }
});

