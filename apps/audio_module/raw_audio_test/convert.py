
import sys

infile = open('test.raw')
outfile = open('testout.raw','w')

for line in infile:
    sample = int(line)
    sample = sample-2047 #zero shift
    sample = sample*16;

    b = bytearray(2)
    b[1] = ((sample & 0xff00) >> 8)
    b[0] = ((sample & 0x00ff))
    outfile.write(b)
