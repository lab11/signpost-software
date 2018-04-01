#!/usr/bin/env node

/*

Takes ambient packets from signposts and publishes them to Wunderground.

Currently requires that a signpost is already registered as a weather station.

*/

var fs   = require('fs');
var mqtt = require('mqtt');
var PWS  = require('wunderground-pws');
var dewpoint = require('dewpoint');

/* File with JSON of the form:
 *
 * {
 *     "SIGNPOST_MAC": {
 *         "id": "STATION_ID",
 *         "password": "STATION_PASSWORD"
 *     }
 * }
 */
var stations = require('/etc/signpost/lab11/wunderground-stations.json');

/*  Ambient Packet:
{
  "device":"signpost_ambient",
  "temperature_c":30.84,
  "humidity":26.4,
  "light_lux":31,
  "pressure_pascals":1677720.5,
  "_meta":{
    "device_id":"c098e5120003",
    "gateway_id":"signpost",
    "received_time":"2017-10-09T21:19:26.024Z",
    "receiver":"lora",
    "geohash":"9q9p3ywdzky",
    "sequence_number":26
}}
*/
var TOPIC_SIGNPOST_AMBIENT1 = 'signpost/processed/signpost/+/lab11/ambient/tphl';
var TOPIC_SIGNPOST_AMBIENT2 = 'signpost/processed/signpost/+/lab11/ambient';


/* Create PWS objects for each signpost we know about */
var signpost_pws = {};
for (var mac in stations) {
    if (!stations.hasOwnProperty(mac)) continue;

    var id       = stations[mac]['id'];
    var password = stations[mac]['password'];

    //console.log(mac);
    //console.log(id);
    //console.log(password);
    signpost_pws[mac] = new PWS(id, password);
}


//The pressure argument only impacts absolute humidity, which we aren't using
var xdp = new dewpoint(0);

var mqtt_client = mqtt.connect('mqtt://localhost');
mqtt_client.subscribe(TOPIC_SIGNPOST_AMBIENT1);
mqtt_client.subscribe(TOPIC_SIGNPOST_AMBIENT2);

mqtt_client.on('connect', function () {
    console.log('Connected to MQTT');


    // Called when we get a packet from MQTT
    mqtt_client.on('message', function (topic, message) {
        var packet = JSON.parse(message.toString());

        var mac = packet._meta.device_id;
        var pws = signpost_pws[mac];
        if (pws != undefined) {
            // Valid observations:
            // https://github.com/fauria/wunderground-pws/blob/master/lib/wunderground-pws.js#L14
            var tempf = (packet.temperature_c * 1.8) + 32;
            var barin = (packet.pressure_pascals * 0.00029530);
            var dewptobj = xdp.Calc(packet.temperature_c, packet.humidity);
            var dewptc = dewptobj.dp;
            var dewptf = (dewptc * 1.8) + 32;
            pws.setObservations({
                tempf: tempf,
                humidity: packet.humidity,
                baromin: barin,
                dewptf: dewptf
            });

            console.log('Posting ' + tempf + ' ' + packet.humidity + ' for ' + mac);
            pws.sendObservations(function(err, success) {
                if (err != null) {
                    console.log(err);
                }
            });
        } else {
            console.log('ERR: No weather station for ' + mac);
            console.log('     Geohash is ' + packet._meta.geohash);
        }
    });
});
