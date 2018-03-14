#!/usr/bin/python

import sys
import argparse
import os.path
import os
import shutil
import subprocess

parser = argparse.ArgumentParser(description='./deploy ../path/to/app/binary version_string output_folder')

parser.add_argument('path', action="store", type=str)
parser.add_argument('version', action="store", type=str)
parser.add_argument('folder', action="store", type=str)

result = parser.parse_args()

path = result.path
version = result.version
folder = result.folder


#first - does the application binary exist?
if(not os.path.isfile(path) or path[-3:] != 'bin'):
    print('You include a path to a valid application binary!')
    sys.exit(1)

#now move the file to the output folder, creating it if it doesn't exist
try:
    os.makedirs(folder)
except OSError as e:
    if e.errno != os.errno.EEXIST:
        print('Error creating directory!')
        sys.exit(1)

shutil.copy(path,folder)
oldfname = os.path.split(path)[1]
len = 0
crcstring = ''
if(folder[-1] == '/'):
    os.rename(folder+oldfname,folder+'app.bin')
    sinfo = os.stat(folder+'app.bin')
    len = sinfo.st_size
    crcstring = subprocess.check_output(['crc32',folder+'app.bin'])
else:
    os.rename(folder+'/'+oldfname,folder+'/'+'app.bin')
    sinfo = os.stat(folder+'/'+'app.bin')
    len = sinfo.st_size
    crcstring = subprocess.check_output(['crc32',folder+'/'+'app.bin'])



#now create the info.txt file
inffile = None
if(folder[-1] == '/'):
    infofile = open(folder+'info.txt','w')
else:
    infofile = open(folder+'/'+'info.txt','w')

#write the version
infofile.write(version+'\n')

#get and write the length in decimal
infofile.write('0x{:02x}'.format(len)+'\n')

#write the crc
infofile.write('0x' + crcstring)
infofile.close()

print('Created deployment folder successfully!')


