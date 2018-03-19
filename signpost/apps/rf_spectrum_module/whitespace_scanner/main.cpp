#include "mbed.h"
#include <stdio.h>

#include "port_signpost.h"
#include "signpost_api.h"
#include "signbus_io_interface.h"
#include "board.h"

#include "RFExplorer_3GP_IoT.h"
#include "RFESweepData.h"
#include "RFECommonValues.h"

RFExplorer_3GP_IoT RF;
DigitalOut Antenna1(ANT1);
DigitalOut Antenna2(ANT2);
DigitalOut Antenna3(ANT3);
DigitalOut Gate(POWER_GATE);

static int8_t bin_max[80] = {0};
static int bin_accumulator[80] = {0};
static int std_accumulator[80] = {0};
static int8_t bin_std[80] = {0};
static int8_t bin_mean[80] = {0};

static void send_packets(void) {
    static uint8_t send_buf[80];
    int ret;

    memcpy(send_buf,bin_max,80);
    ret = signpost_networking_publish("ws_max",send_buf,81);
    if(ret < 0 ) printf("Sending max error!\n");

    memcpy(send_buf,bin_mean,80);
    ret = signpost_networking_publish("ws_mean",send_buf,81);
    if(ret < 0 ) printf("Sending mean error!\n");

    memcpy(send_buf,bin_std,80);
    ret = signpost_networking_publish("ws_std",send_buf,81);
    if(ret < 0 ) printf("Sending std error!\n");
}

int main(void) {
    printf("Testing mbed Initialization\n");

    //signpost initialization
    int rc;
    do {
        rc = signpost_init("lab11/spectrum");
        if (rc < 0) {
            printf(" - Error initializing module (code: %d). Sleeping 5s.\n", rc);
            port_signpost_delay_ms(5000);
        }
    } while (rc < 0);

    printf("Initializing RF Explorer...\n");

    Gate = 0;
    Antenna1 = 1;
    Antenna2 = 0;
    Antenna3 = 0;

    //initialize bin_max
    for(uint8_t i = 0; i < 80; i++) {
        bin_max[i] = -127;
    }

    //reset
    printf("Resetting Hardware\n");
    RF.resetHardware();
    wait_ms(5000);
    RF.changeBaudrate(500000);
    wait_ms(1000);
    RF.changeBaudrate(115200);
    wait_ms(1000);

    //initialize
    printf("Calling Init\n");
    RF.init();

    wait_ms(1000);

    //change the baud rate
    wait_ms(1000);

    //configure the module to scan the US LoRa frequency bands
    RF.sendNewConfig(470000,950000);

    static int sweep_counter = 0;
    while(1) {
        unsigned short int nProcessResult = RF.processReceivedString();

        //If received data processing was correct, we can use it
        if (nProcessResult == _RFE_SUCCESS)
        {
            if ((RF.getLastMessage() == _CONFIG_MESSAGE))
            {
                //Message received is a new configuration from 3G+
                //We show new Start/Stop KHZ range here from the new configuration
                /*printf("New Config\n");
                printf("StartKHz: ");
                printf("%lu ", RF.getConfiguration()->getStartKHZ());
                printf("StopKHz:  ");
                printf("%lu ", RF.getConfiguration()->getEndKHZ());
                printf("StepHz:  ");
                printf("%lu \n", RF.getConfiguration()->getStepHZ());*/
            }
            else if((RF.getLastMessage() == _SWEEP_MESSAGE) && RF.isValid())
            {
                //Message received was actual sweep data, we can now use internal functions
                //loop through the sweep data adding to the accumulators
                //go by three so we get the 6MHZ tv channel binning
                printf("Got sweep\n");
                RFESweepData* data = RF.getSweepData();
                for(int i = 0; i < 240; i += 3) {
                    int16_t one;
                    int16_t two;
                    int16_t three;
                    one = data->getAmplitudeDBM(i);
                    two = data->getAmplitudeDBM(i+1);
                    three = data->getAmplitudeDBM(i+2);
                    int16_t av = (one+two+three)/3;
                    int16_t imax = ((one) > (two) ? (one):(two));
                    int16_t max = ((imax) > (three) ? (imax):(three));

                    //record the maximum
                    if(bin_max[i/3] < max) {
                        bin_max[i/3] = max;
                    }

                    //add to the accumulator
                    bin_accumulator[i/3] += av;

                    //by recording the square of expectations we can calculate
                    //the std deviation post-scan
                    std_accumulator[i/3] += one*one;
                    std_accumulator[i/3] += two*two;
                    std_accumulator[i/3] += three*three;
                }
                sweep_counter++;

                if(sweep_counter > 30) {
                    printf("Got 30 sweeps\n");
                    //calculate the metrics
                    for(int i = 0; i < 80; i++) {
                        bin_mean[i] = bin_accumulator[i]/sweep_counter;
                        bin_std[i] = sqrt(std_accumulator[i]/(sweep_counter*3) - bin_mean[i]*bin_mean[i]);
                    }

                    //send the data
                    printf("Sending packets\n");
                    send_packets();

                    //go to sleep for some period of time
                    printf("Duty cycling\n");
                    int ret;
                    do {
                        ret = signpost_energy_duty_cycle(240000);
                        if(ret < 0) {
                            wait_ms(5000);
                        }
                    } while(ret < 0);
                }
            }
        } else {
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
