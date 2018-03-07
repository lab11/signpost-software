Audio Module
============

The core application that we currently run on the audio module is the 
spectrum_volum_reporter. This application takes samples in 7 frequency bins(filtered in hardware by the MSGEQ7)
every second, then sends them over the networking interface every 10 seconds
with a timestamp. This allows us to perform analysis like vehicle detection
in the cloud.

Simple testing applications can be found in the test folder.
