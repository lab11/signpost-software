import sys
import os

from periphery import GPIO
import signpost
import subprocess

def main():

    #declare the gpio interrupt that signals there
    #is an RPC to service
    pending_rpc_gpio = GPIO(111, "in")
    EdisonSignbus = signpost.EdisonApiClient()

    while True:

        print("Woke up!")
        proclist = []
        while True:

            #check if we have a new RPC
            is_rpc = pending_rpc_gpio.read()
            if(is_rpc):
                #if we do have a new RPC launch the process and add it to the list
                rpc_buf = EdisonSignbus.read_RPC()

                #we should eventually make this secure and make user(s)
                #for module(s) with very limited permissions
                rpc_user = rpc_buf[0]
                p = subprocess.Popen(rpc_buf[1:])
                print("Process {} started with PID {}".format(rpc_buf[1:],p.pid()))
                proclist.append((rpc_user,p))

            #check if any of the process in the list have finished and remove them
            for proc in proclist:
                if proc[1].poll():
                    #the process is done - pop it from the list
                    print("Process PID {} finished, removing from list".format(proc[1].pid()))
                    proclist.remove(proc)

            #if all processes have finished break so that we can sleep
            if not proclist:
                break;

        #We are done doing stuff - go back to sleep
        print("Done servicing RPCs - going to sleep")
        os.system("echo mem > /sys/power/state")


if __name__ == "__main__":
    main()
