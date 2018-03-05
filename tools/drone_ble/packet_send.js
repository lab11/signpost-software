#!/usr/bin/env node

var http = require('http')
var fs = require('fs')

var dirname = "packets/"
if (!fs.existsSync(dirname)) {
  console.log("No packets to send!")
  process.exit()
}

function post_callback (res) {
  var response_string = ""
    res.setEncoding('utf8');
  res.on('data', function (data) {
    response_string += data
  })
  res.on("end", function () {
    console.log('Got response from aws!')
  })
}


var filelist = []
fs.readdirSync(dirname).forEach (file =>{
  filelist.push(file)
  console.log(file)
})
console.log("Sending " + filelist.length.toString() + " collected packets")
for(var i = 0; i < filelist.length; i++) {
  var filename = dirname + filelist[i]
  var packet = fs.readFileSync(filename)
  var post_options = {
    host: 'ec2-35-166-179-172.us-west-2.compute.amazonaws.com',
    path: '/signpost',
    port: '80',
    method: 'POST',
    headers: {
      'Content-Type': 'application/octet-stream',
      'Content-Length': Buffer.byteLength(packet)
    }
  }

  var post_req = http.request(post_options, post_callback)
  post_req.write(packet)
  post_req.end()
}
