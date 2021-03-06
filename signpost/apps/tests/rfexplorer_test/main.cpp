#include "mbed.h"
#include <stdio.h>

#include "port_signpost.h"
#include "signpost_api.h"
#include "signbus_io_interface.h"

#include "RFExplorer_3GP_IoT.h"
#include "RFECommonValues.h"

RFExplorer_3GP_IoT RF;
DigitalOut RF_gpio2(_RFE_GPIO2);
DigitalOut Antenna1(PA_4);
DigitalOut Antenna2(PA_5);
DigitalOut Antenna3(PA_6);

int main(void) {
    printf("Testing mbed Initialization\n");

    //signpost initialization
    int rc;
    do {
        rc = signpost_init("test","rfexp");
        if (rc < 0) {
            printf(" - Error initializing module (code: %d). Sleeping 5s.\n", rc);
            port_signpost_delay_ms(5000);
        }
    } while (rc < 0);

    printf("Initializing RF Explorer...\n");

    Antenna1 = 1;
    Antenna2 = 0;
    Antenna3 = 0;

    //set GPIO2 to 0 before reset for 2400 baud
    RF_gpio2 = 0;

    //reset
    printf("Resetting Hardware\n");
    RF.resetHardware();
    wait_ms(5000);

    //initialize
    printf("Calling Init\n");
    RF.init();

    //after init we can pull this back up
    RF_gpio2 = 1;
    wait_ms(1000);

    //change the baud rate
    printf("Changing baud rate\n");
    RF.changeBaudrate(115200);
    wait_ms(1000);

    //configure the module to scan the US LoRa frequency bands
    RF.sendNewConfig(902300,914300);

    while(1) {
        unsigned short int nProcessResult = RF.processReceivedString();

        //If received data processing was correct, we can use it
        if (nProcessResult == _RFE_SUCCESS)
        {
            if ((RF.getLastMessage() == _CONFIG_MESSAGE))
            {
                //Message received is a new configuration from 3G+
                //We show new Start/Stop KHZ range here from the new configuration
                printf("New Config\n");
                printf("StartKHz: ");
                printf("%lu ", RF.getConfiguration()->getStartKHZ());
                printf("StopKHz:  ");
                printf("%lu ", RF.getConfiguration()->getEndKHZ());
                printf("StepHz:  ");
                printf("%lu \n", RF.getConfiguration()->getStepHZ());
            }
            else if((RF.getLastMessage() == _SWEEP_MESSAGE) && RF.isValid())
            {
                //Message received was actual sweep data, we can now use internal functions
                //to get sweep data parameters
                unsigned long int nFreqPeakKHZ=0;
                int16_t nPeakDBM=0;
                if (RF.getPeak(&nFreqPeakKHZ, &nPeakDBM) ==_RFE_SUCCESS)
                {
                    //Display frequency and amplitude of the signal peak
                    printf("%lu ", nFreqPeakKHZ);
                    printf(" KHz at ");
                    printf("%d", nPeakDBM);
                    printf(" dBm\n");
                }
            }
        }
        else
        {
            //Message or Data was not received, or was wrong. Note:
            // _RFE_IGNORE or _RFE_NOT_MESSAGE are not errors, it just mean a new message was not available
            if ((nProcessResult != _RFE_IGNORE) && (nProcessResult != _RFE_NOT_MESSAGE))
            {
                //Other error codes were received, report for information
                //Check library error codes for more details
                printf("Error: ");
                printf("%d\n", nProcessResult);
            }
        }
    }
}
