#!/usr/env/bin python

import os
import sys
import requests
from requests.auth import HTTPBasicAuth

import gdp_auth

print (gdp_auth.password)
print (gdp_auth.username)

#create a test gcl

print("Creating a GCL - it might already exist")

r = requests.put('https://gdp-rest-01.eecs.berkeley.edu/gdp/v1/gcl',
                auth=HTTPBasicAuth(gdp_auth.username,gdp_auth.password),
                json={"external-name":"edu.berkeley.eecs.lab11.signpost.firsttest",
                        "-C":"adkins@berkeley.edu"})

print(r.status_code)
print(r.text)

print("Appending to the test GCL")
r = requests.post('https://gdp-rest-01.eecs.berkeley.edu/gdp/v1/gcl/edu.berkeley.eecs.lab11.signpost.firsttest',
                auth=HTTPBasicAuth(gdp_auth.username,gdp_auth.password),
                json={"Testdata":"lab11"})

print(r.status_code)
print(r.text)
