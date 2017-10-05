var noble = require('noble')
var async = require('async')
var sleep = require('sleep')
var request = require('request')
var fs = require('fs')

var signpost_service_uuicd = '6f00'
var signpost_update_char_uuid = '6f01' // use this to write request to signpost
var signpost_notify_char_uuid = '6f02' // use this to receive data from signpost
var signpost_ack_char_uuid = '' // use this characteristic to write acks to the 

var signpost_service = 0
var signpost_update_char = 0
var signpost_notify_char = 0
var signpost_ack_char = 0

var dataBuilder = null

noble.on('stateChange', function(state) {
  if (state === 'poweredOn') {
    noble.startScanning();
  } else {
    noble.stopScanning();
  }
})

noble.on('discover', on_discovery)

function on_discovery(peripheral) {
	var advertisement = peripheral.advertisement;

	var localName = advertisement.localName;
  var ble_address = advertisement.address;

	if (localName) {
	  console.log('Found peripheral with localName ' + localName)
	  if(localName == "Signpost") {
	  	noble.stopScanning()
	  	explore(peripheral) // set up disconnect callback and connect to the peripheral
	  }
	}
	else {
		console.log('Found peripheral with no localname')
	}
}

function explore(peripheral) {
  console.log('services and characteristics:');

  peripheral.on('disconnect', function() {
      console.log("disconnect")
      noble.startScanning();
  });

  peripheral.connect(function(error) { peripheral.discoverServices([], on_discover_services); });
}

function on_discover_services(error, services) {
      for(i = 0; i < services.length; i++) {
      	var service = services[i]
        console.log("Found service with uuid " + service.uuid)
      	if (service.uuid == signpost_service_uuicd) {
      		console.log("Found signpost service")
            signpost_service = service
      		service.discoverCharacteristics([], on_discover_characteristics)
      		break
      	}
      }
}

function on_discover_characteristics(error, characteristics) {
	console.log("Discover characteristics")
	for(i = 0; i < characteristics.length; i++) {
		var characteristic = characteristics[i]
        console.log("Found char with uuid " + characteristic.uuid)
        if (characteristic.uuid == signpost_update_char_uuid) {
            signpost_update_char = characteristic
        }
        else if (characteristic.uuid == signpost_notify_char_uuid) {
            signpost_notify_char = characteristic
            signpost_notify_char.notify(true, function(err) {console.log("Enabled notify on signpost update char")})
            signpost_notify_char.on('data', on_signpost_notify_data) // Callback for when we get new data
        }
        else if (characteristic.uuid == signpost_ack_char_uuid) {
            signpost_ack_char = characteristic
        }
	}
	console.log("Time to request some data")
	var buffer = Buffer.from("test") // put whatever string you need here 
	//sleep.sleep(2)
	//console.log("Write sleep done")
	signpost_update_char.write(buffer, false, function() {console.log("Wrote request to signpost_u")})
}

// data is a nodejs buffer.
// you can use functions like data.readUInt8 to get data from the node. 
function on_signpost_notify_data(data, isNotification) {
    console.log("Got some data from the signpost")
    // do some shit involving request?

    // Write ack back
    var buffer = Buffer.from([0x01]) 
    signpost_ack_char.write(buffer)
}


