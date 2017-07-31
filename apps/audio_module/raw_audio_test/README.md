Raw Audio Test
==============

This will test the microphone on the signpost

To use it recompile the kernel to print at 1000000Mbaud

The app will then print raw values out the debug UART,
which you can paste into a file, convert to a raw audio file
using the convert.py script, and play using the linux play command.
