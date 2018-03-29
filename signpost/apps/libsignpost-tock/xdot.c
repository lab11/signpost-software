#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "tock.h"
#include "console.h"
#include "led.h"
#include "gpio.h"
#include "timer.h"
#include "xdot.h"
#include "multi_console.h"
#include "at_command.h"

#define LORA_CONSOLE 0x10080007

#define LORA_WAKE_PIN 10

int xdot_init(void) {
    gpio_enable_output(LORA_WAKE_PIN);

    int ret = at_send(LORA_CONSOLE, "ATE0\n");
    if(ret < 0) return XDOT_ERROR;

    ret = at_wait_for_response(LORA_CONSOLE,3,500);
    if(ret < 0) return XDOT_ERROR;
    else return XDOT_SUCCESS;
}

int xdot_join_network(uint8_t* AppEUI, uint8_t* AppKey) {
    int ret = at_send(LORA_CONSOLE, "AT\n");
    if(ret < 0) return XDOT_ERROR;

    ret = at_wait_for_response(LORA_CONSOLE,3,500);
    if(ret < 0) return XDOT_ERROR;

    ret = at_send(LORA_CONSOLE, "AT+NJM=1\n");
    if(ret < 0) return XDOT_ERROR;

    ret = at_wait_for_response(LORA_CONSOLE,3,500);
    if(ret < 0) return XDOT_ERROR;


    ret = at_send(LORA_CONSOLE, "AT+NI=0,");
    if(ret < 0) return XDOT_ERROR;

    char c[200];
    uint8_t len = 0;
    char* cpt = c;
    for(uint8_t i = 0; i < 8; i++) {
        snprintf(cpt,3,"%02X",AppEUI[i]);
        cpt += 2;
        len += 2;
    }

    ret = at_send_buf(LORA_CONSOLE,(uint8_t*)c,len);
    if(ret < 0) return XDOT_ERROR;

    ret = at_send(LORA_CONSOLE,"\n");
    if(ret < 0) return XDOT_ERROR;

    ret = at_wait_for_response(LORA_CONSOLE,6,500);
    if(ret < 0) return XDOT_ERROR;


    ret = at_send(LORA_CONSOLE, "AT+NK=0,");
    if(ret < 0) return XDOT_ERROR;

    len = 0;
    cpt = c;
    for(uint8_t i = 0; i < 16; i++) {
        snprintf(cpt,3,"%02X",AppKey[i]);
        cpt += 2;
        len += 2;
    }

    ret = at_send_buf(LORA_CONSOLE,(uint8_t*)c,len);
    if(ret < 0) return XDOT_ERROR;

    ret = at_send(LORA_CONSOLE,"\n");
    if(ret < 0) return XDOT_ERROR;

    ret = at_wait_for_response(LORA_CONSOLE,6,500);
    if(ret < 0) return XDOT_ERROR;

    ret = at_send(LORA_CONSOLE,"AT+PN=1\n");
    if(ret < 0) return XDOT_ERROR;
    ret = at_wait_for_response(LORA_CONSOLE,3,500);
    if(ret < 0) return XDOT_ERROR;

    ret = at_send(LORA_CONSOLE,"AT+FSB=1\n");
    if(ret < 0) return XDOT_ERROR;
    ret = at_wait_for_response(LORA_CONSOLE,3,500);
    if(ret < 0) return XDOT_ERROR;

    ret = at_send(LORA_CONSOLE,"AT&W\n");
    if(ret < 0) return XDOT_ERROR;
    ret = at_wait_for_response(LORA_CONSOLE,3,2000);
    if(ret < 0) return XDOT_ERROR;

    ret = xdot_reset();
    if(ret < 0) return XDOT_ERROR;

    ret = at_send(LORA_CONSOLE,"AT+JOIN\n");
    if(ret < 0) return XDOT_ERROR;

    ret = at_wait_for_response(LORA_CONSOLE,3,15000);
    if(ret < 0) return XDOT_ERROR;

    return XDOT_SUCCESS;
}

int xdot_get_txdr(void) {

    int ret = at_send(LORA_CONSOLE,"AT+TXDR?\n");
    if(ret < 0) return XDOT_ERROR;

    uint8_t buf[200];
    ret = at_get_response(LORA_CONSOLE, 3, 500, buf, 200);

    if(ret <= 0) {
        return ret;
    }

    if(buf[0] == '0') {
        return 0;
    } else {
        char c[2];
        snprintf(c,1,"%s",(char*)buf);
        int a = atoi(c);
        if(a == 0) {
            return XDOT_ERROR;
        } else {
            return a;
        }
    }
}

int xdot_set_txdr(uint8_t dr) {

    if(dr > 4) {
        return XDOT_INVALID_PARAM;
    }

    char cmd[15];
    snprintf(cmd,15, "AT+TXDR=%u\n", dr);
    int ret = at_send(LORA_CONSOLE, cmd);
    if(ret < 0) return XDOT_ERROR;

    return at_wait_for_response(LORA_CONSOLE,3,500);
}

int xdot_set_adr(uint8_t adr) {

    if(adr > 1) {
        return XDOT_INVALID_PARAM;
    }

    int ret = at_send(LORA_CONSOLE,"AT+ADR=");
    if(ret < 0) return XDOT_ERROR;

    char cmd[4];
    snprintf(cmd,4, "%u\n", adr);

    ret = at_send(LORA_CONSOLE,cmd);
    if(ret < 0) return XDOT_ERROR;

    return at_wait_for_response(LORA_CONSOLE,3,500);
}

int xdot_set_txpwr(uint8_t tx) {

    if(tx > 20) {
        return XDOT_INVALID_PARAM;
    }

    int ret = at_send(LORA_CONSOLE,"AT+TXP=");
    if(ret < 0) return XDOT_ERROR;

    char cmd[4];
    snprintf(cmd,4, "%u\n", tx);
    ret = at_send(LORA_CONSOLE,cmd);
    if(ret < 0) return XDOT_ERROR;

    return at_wait_for_response(LORA_CONSOLE,3,500);
}

int xdot_set_ack(uint8_t ack) {

    if(ack > 8) {
        return XDOT_INVALID_PARAM;
    }

    int ret = at_send(LORA_CONSOLE,"AT+ACK=");
    if(ret < 0) return XDOT_ERROR;

    char cmd[4];
    snprintf(cmd,4, "%u\n",ack);
    ret = at_send(LORA_CONSOLE,cmd);
    if(ret < 0) return XDOT_ERROR;

    return at_wait_for_response(LORA_CONSOLE,3,500);
}

int xdot_save_settings(void) {
    int ret = at_send(LORA_CONSOLE, "AT&W\n");
    if(ret < 0) return XDOT_ERROR;

    return at_wait_for_response(LORA_CONSOLE,3,2000);
}

int xdot_reset(void) {

    int ret = at_send(LORA_CONSOLE, "ATZ\n");
    if(ret < 0) return XDOT_ERROR;

    ret = at_wait_for_response(LORA_CONSOLE,3,500);
    if(ret < 0) return XDOT_ERROR;

    for(volatile uint32_t i = 0; i < 1500000; i++);

    ret = at_send(LORA_CONSOLE, "AT\n");
    if(ret < 0) return XDOT_ERROR;
    delay_ms(100);

    //return at_wait_for_response(LORA_CONSOLE,6,500);
    return XDOT_SUCCESS;
}

static void buf_to_hex_string(char* dest_buf, uint32_t dest_len, uint8_t* source_buf, uint32_t source_len) {
    char* cpt = dest_buf;
    for(uint32_t i = 0; i < source_len && i*2 < dest_len; i++) {
        snprintf(cpt,3,"%02X",source_buf[i]);
        cpt += 2;
    }
}

int xdot_send(uint8_t* buf, uint8_t len) {

    char* buf_str = malloc(len*2+1);
    if(!buf_str) return XDOT_ERROR;

    buf_to_hex_string(buf_str, len*2+1, buf, len);

    int ret = at_send(LORA_CONSOLE, "AT+sendb=");
    if(ret < 0)  {
        free(buf_str);
        return XDOT_ERROR;
    }

    ret = at_send_buf(LORA_CONSOLE, (uint8_t*)buf_str,len*2);
    if(ret < 0)  {
        free(buf_str);
        return XDOT_ERROR;
    }

    ret = at_send(LORA_CONSOLE, "\n");
    if(ret < 0)  {
        free(buf_str);
        return XDOT_ERROR;
    }

    free(buf_str);
    return at_wait_for_response(LORA_CONSOLE,3,5000);
}

int xdot_receive(uint8_t* buf, uint8_t len) {

    uint8_t* buf_str = malloc(len*2+9);
    if(!buf_str) return XDOT_ERROR;

    int ret = at_send(LORA_CONSOLE, "AT+RECV\n");
    if(ret < 0)  {
        free(buf_str);
        return XDOT_ERROR;
    }

    ret = at_get_response(LORA_CONSOLE, 3, 3000, buf_str, len*2);
    if(ret < 0)  {
        free(buf_str);
        return XDOT_ERROR;
    }

    if(ret < 9) {
        //something went wrong - we should get at least 9 characters
        free(buf_str);
        return XDOT_ERROR;
    } else if (ret == 9) {
        //we just got not data return 0
        return 0;
    } else {
        //when we get data it throws two more chars in for some reason??
        ret -= 11;
    }

    for(uint16_t i = 0; i < ret/2 && i < len; i++) {
        char tbuf[5] = {'0','x',buf_str[3+i*2],buf_str[4+i*2], 0};
        buf[i] = strtol(tbuf, NULL, 0);
    }

    return ret/2;
}

int xdot_sleep(void) {
    int ret = at_send(LORA_CONSOLE, "AT+WM=1\n");
    if(ret < 0) return XDOT_ERROR;

    ret = at_wait_for_response(LORA_CONSOLE,3,500);
    if(ret < 0) return XDOT_ERROR;

    ret = at_send(LORA_CONSOLE, "AT+sleep=1\n");
    if(ret < 0) return XDOT_ERROR;

    return at_wait_for_response(LORA_CONSOLE,3,500);
}

int xdot_wake(void) {
    gpio_toggle(LORA_WAKE_PIN);
    gpio_toggle(LORA_WAKE_PIN);

    for(volatile uint32_t i = 0; i < 15000; i++);

    return XDOT_SUCCESS;
}
