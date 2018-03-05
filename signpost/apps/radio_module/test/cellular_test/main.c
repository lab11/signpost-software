#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#include "tock.h"
#include "timer.h"
#include "console.h"
#include "led.h"
#include "sara_u260.h"


int main (void) {
    printf("Starting Cell Test\n");

    delay_ms(2000);

    sara_u260_init();

    delay_ms(10000);

    uint8_t buf[5] = {'L','a','b','1','1'};
    printf("Posting...\n");
    int ret = sara_u260_basic_http_post("httpbin.org","/post",buf,5);

    if(ret < SARA_U260_SUCCESS) {
        if(ret == SARA_U260_NO_SERVICE) {
            printf("No Service! try again in a bit.\n");
        } else {
            printf("Post failed with error code %d\n",ret);
        }
    }

    if(ret >= 0) {
        uint8_t dbuf[500] = {0};
        printf("Getting response\n");
        int l = sara_u260_get_post_response(dbuf, 499);
        if(l < 0) {
            printf("Did not get response\n");
        } else {
            dbuf[l] = 0;
            printf("Got response of length %d: \n",l);
            printf("%s\n", (char*)dbuf);
        }
    }


    printf("Now trying http get\n");
    ret = sara_u260_basic_http_get("rawgit.com","/lab11/signpost-software/master/apps/radio_module/cellular_test/README.md");

    if(ret < SARA_U260_SUCCESS) {
        if(ret == SARA_U260_NO_SERVICE) {
            printf("No Service! try again in a bit.\n");
        } else {
            printf("Get failed with error code %d\n",ret);
        }
    }

    if(ret >= 0) {
        uint8_t dbuf[1000] = {0};
        printf("Getting response\n");
        int l = sara_u260_get_get_response(dbuf, 999);
        if(l < 0) {
            printf("Did not get response\n");
        } else {
            dbuf[l] = 0;
            printf("Got response of length %d: \n",l);
            printf("%s\n", (char*)dbuf);
        }
    }
}
