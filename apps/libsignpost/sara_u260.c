#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "at_command.h"
#include "sara_u260.h"
#include "tock.h"

#define SARA_CONSOLE 110

int sara_u260_init(void) {
    int ret = at_send(SARA_CONSOLE,"AT\r");
    if (ret < 0) return SARA_U260_ERROR;

    for(volatile uint32_t i = 0; i < 15000; i++);

    ret = at_send(SARA_CONSOLE,"ATE0\r");
    if (ret < 0) return SARA_U260_ERROR;

    ret = at_wait_for_response(SARA_CONSOLE,3);
    if(ret >= AT_SUCCESS) {
        return SARA_U260_SUCCESS;
    } else {
        return SARA_U260_ERROR;
    }
}

static int sara_u260_check_connection(void) {
    int ret = at_send(SARA_CONSOLE, "AT+COPS?\r");
    if (ret < 0) return SARA_U260_ERROR;

    //We just need to check how many bytes it will return
    //This tells us if the operator field is populated (has service)
    uint8_t buf[50];
    int len = at_get_response(SARA_CONSOLE, 3, buf, 50);

    if(len <= 0) {
        return SARA_U260_ERROR;
    }else if(len > 20) {
        return SARA_U260_SUCCESS;
    } else {
        return SARA_U260_NO_SERVICE;
    }
}

static int sara_u260_setup_packet_switch(void) {
    //check the connection
    int ret = sara_u260_check_connection();
    if(ret != SARA_U260_SUCCESS) {
        return ret;
    }

    //do we already have a pack switch setup?
    ret = at_send(SARA_CONSOLE,"AT+UPSND=0,0\r");
    if (ret < 0) return SARA_U260_ERROR;

    ret = at_wait_for_response(SARA_CONSOLE, 3);
    if(ret == AT_ERROR) {
        //no we need to set one up

        //set the apn for our network - it can be blank
        ret = at_send(SARA_CONSOLE, "AT+UPSD=0,1,\"\"\r");
        if (ret < 0) return SARA_U260_ERROR;

        ret = at_wait_for_response(SARA_CONSOLE, 3);
        if (ret < 0) return SARA_U260_ERROR;

        //request to connect
        ret = at_send(SARA_CONSOLE, "AT+UPSDA=0,3\r");
        if (ret < 0) return SARA_U260_ERROR;

        ret = at_wait_for_response(SARA_CONSOLE, 3);
        if (ret < 0) return SARA_U260_ERROR;

        //did it work
        ret = at_send(SARA_CONSOLE,"AT+UPSND=0,0\r");
        if (ret < 0) return SARA_U260_ERROR;

        ret = at_wait_for_response(SARA_CONSOLE, 3);

        if(ret < 0) {
            //no it didn't - return error
            return SARA_U260_ERROR;
        }

    } else if (ret == AT_NO_RESPONSE) {
        //nothign to do about this - return an error
        return SARA_U260_ERROR;
    }

    return SARA_U260_SUCCESS;
}

static int sara_u260_del_file(const char* fname) {
    int ret;

    ret = at_send(SARA_CONSOLE, "AT+UDELFILE=\"");
    if (ret < 0) return SARA_U260_ERROR;

    ret = at_send(SARA_CONSOLE, fname);
    if (ret < 0) return SARA_U260_ERROR;

    ret = at_send(SARA_CONSOLE, "\"\r");
    if (ret < 0) return SARA_U260_ERROR;

    ret = at_wait_for_response(SARA_CONSOLE,3);
    if(ret >= 0) {
        return SARA_U260_SUCCESS;
    } else {
        return SARA_U260_ERROR;
    }
}

static int sara_u260_write_to_file(const char* fname, uint8_t* buf, size_t len) {
    
    int ret = at_send(SARA_CONSOLE, "AT+UDWNFILE=\"");
    if (ret < 0) return SARA_U260_ERROR;

    ret = at_send(SARA_CONSOLE, fname);
    if (ret < 0) return SARA_U260_ERROR;

    ret = at_send(SARA_CONSOLE, "\",");
    if (ret < 0) return SARA_U260_ERROR;

    char c[15];
    int clen = snprintf(c,15,"%lu",(uint32_t)len);
    if(clen <= 0) {
        return SARA_U260_ERROR; 
    }

    ret = at_send(SARA_CONSOLE, c);
    if (ret < 0) return SARA_U260_ERROR;

    ret = at_send(SARA_CONSOLE, "\r");
    if (ret < 0) return SARA_U260_ERROR;

    ret = at_wait_for_custom_response(SARA_CONSOLE,3,"\n>");
    if (ret < 0) return SARA_U260_ERROR;

    //now send the buffer in chunks of 30
    for(size_t i = 0; i < len; i+=30) {
        if(i+30 <= len) {
            ret = at_send_buf(SARA_CONSOLE, buf+i, 30);
            if (ret < 0) return SARA_U260_ERROR;
        } else {
            ret = at_send_buf(SARA_CONSOLE, buf+i, len-i);
            if (ret < 0) return SARA_U260_ERROR;
        }
    }

    ret = at_wait_for_response(SARA_CONSOLE,3);
    if(ret >= AT_SUCCESS) {
        return SARA_U260_SUCCESS;
    } else {
        return SARA_U260_ERROR;
    }
}

int sara_u260_basic_http_post(const char* url, const char* path, uint8_t* buf, size_t len) {

    //make the connection
    int ret = sara_u260_setup_packet_switch();
    if(ret < 0) {
        return ret;
    }

    //delete the file
    ret = sara_u260_del_file("postdata.bin");
    //Don't catch this error - the file might not exist
    /*if(ret < 0) {
        return ret;
    }*/

    //write the data to a file
    ret = sara_u260_write_to_file("postdata.bin", buf, len);
    if(ret < 0) {
        return ret;
    }

    //setup http profile
    ret = at_send(SARA_CONSOLE,"AT+UHTTP=0\r");
    if (ret < 0) return SARA_U260_ERROR;

    ret = at_wait_for_response(SARA_CONSOLE, 3);
    if (ret < 0) return SARA_U260_ERROR;

    ret = at_send(SARA_CONSOLE,"AT+UHTTP=0,1,\"");
    if (ret < 0) return SARA_U260_ERROR;

    ret = at_send(SARA_CONSOLE,url);
    if (ret < 0) return SARA_U260_ERROR;

    ret = at_send(SARA_CONSOLE,"\"\r");
    if (ret < 0) return SARA_U260_ERROR;

    ret = at_wait_for_response(SARA_CONSOLE, 3);
    if (ret < 0) return SARA_U260_ERROR;

    ret = at_send(SARA_CONSOLE,"AT+UHTTP=0,5,80\r");
    if (ret < 0) return SARA_U260_ERROR;

    ret = at_wait_for_response(SARA_CONSOLE, 3);
    if (ret < 0) return SARA_U260_ERROR;

    //now actually do the post
    ret = at_send(SARA_CONSOLE,"AT+UHTTPC=0,4,\"");
    if (ret < 0) return SARA_U260_ERROR;

    ret = at_send(SARA_CONSOLE,path);
    if (ret < 0) return SARA_U260_ERROR;

    ret = at_send(SARA_CONSOLE,"\",\"postresult.txt\",\"postdata.bin\",2\r");
    if (ret < 0) return SARA_U260_ERROR;

    ret = at_wait_for_response(SARA_CONSOLE, 3);

    return ret;
}

int sara_u260_get_post_response(uint8_t* buf, size_t max_len) {
    return sara_u260_get_post_partial_response(buf, 0, max_len);
}

int sara_u260_get_post_partial_response(uint8_t* buf, size_t offset, size_t max_len) {
    
    int ret = at_send(SARA_CONSOLE,"AT+URDBLOCK=\"postresult.txt\",");
    if (ret < 0) return SARA_U260_ERROR;


    
    char c[60];
    snprintf(c,60,"%lu,%lu\r",(uint32_t)offset,(uint32_t)max_len);
    ret = at_send(SARA_CONSOLE,c);
    if (ret < 0) return SARA_U260_ERROR;

    //should return data plus some framing characters, so take the
    //data length being returned and add some room

    int len = max_len+100;
    uint8_t* tbuf = (uint8_t*)malloc(max_len*sizeof(uint8_t)+100);
    if(!tbuf) {
        return SARA_U260_ERROR;
    }

    ret = at_get_response(SARA_CONSOLE,3,tbuf,len);
    len = ret;

    if(ret < 0) {
        free(tbuf);

        return SARA_U260_ERROR;
    }

    //okay we actually got some data, let's copy it into the buffer
    //first find the length
    int c1 = 0;
    int c2 = 0;
    for(int i = 0; i < len; i++) {
        if(tbuf[i] == ',') {
            if(c1 == 0) {
                c1 = i;
            } else if(c2 == 0) {
                c2 = i;
                break;
            } else {
                break;
            }
        }
    }

    if(c1 == 0 || c2 ==0) {
        free(tbuf);
        return SARA_U260_ERROR;
    }

    char dl[10] = {0};
    memcpy(dl,tbuf+c1+1,c2-c1-1);
    int dlen = atoi(dl);

    if(dlen >=0 && (size_t)dlen >= max_len) {
        free(tbuf);
        return SARA_U260_ERROR;
    }

    //now manually memcpy out the data into tbuf (because there could be nulls)
    memcpy(buf,tbuf+(len - 7 - dlen),dlen);

    free(tbuf);

    return dlen;
}

int sara_u260_get_ops_information(sara_u260_ops_info_t* inf, size_t num_info) {
    int ret = at_send(SARA_CONSOLE,"AT+COPS=6\r"); 
    if (ret < 0) return SARA_U260_ERROR;

    //this size should allow us to receive numinfo neighbors
    uint8_t* retbuf = malloc(num_info*50*sizeof(uint8_t));
    if(!retbuf) {
        return SARA_U260_ERROR;
    }

    int len = at_get_response(SARA_CONSOLE, num_info, retbuf, num_info*50);
    if(len < 0)  return SARA_U260_ERROR;
  
    uint8_t num_fields = 0;
    int line_start_location = 0;
    size_t inf_index = 0;
    for(int i = 0; i < len; i++) {
        if(retbuf[i] == ',') {
            num_fields++;
        } else if(retbuf[i] == '\n') {
            if(num_fields > 5) {
                //the line should never be over 70 characters
                char line[70];
                if(i-line_start_location < 70) {
                    memcpy(line, retbuf+line_start_location, i-line_start_location);
                } else {
                    memcpy(line, retbuf+line_start_location, 70);
                }

                //scanf the line
                if(num_fields == 9) {
                    sscanf(line,"%hu,%hu,%hx,%*d,%lx, %*d, %*d,%*d,%hhu,%*d",&inf[inf_index].mcc,
                                                        &inf[inf_index].mnc,&inf[inf_index].lac,
                                                        &inf[inf_index].ci,&inf[inf_index].rxlev);
                    inf[inf_index].arfcn = 0;
                    inf[inf_index].bsic = 0;
                } else if(num_fields == 6) {
                    sscanf(line,"%hu,%hu,%hx,%lx,%hhu,%hu,%hhu",&inf[inf_index].mcc,&inf[inf_index].mnc,
                                                                &inf[inf_index].lac,&inf[inf_index].ci,
                                                                &inf[inf_index].bsic,&inf[inf_index].arfcn,
                                                                &inf[inf_index].rxlev);
                }
                inf_index++;
                if(inf_index == num_info) {
                    free(retbuf);
                    return inf_index;
                }
            }
            num_fields = 0;
            line_start_location = i;
        }
    }

    free(retbuf);
    return inf_index;
}
