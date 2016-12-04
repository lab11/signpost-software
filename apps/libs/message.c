#include "tock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gpio.h"
#include "i2c_master_slave.h"

#define I2C_MAX_LEN 255

static uint8_t master_write_buf[I2C_MAX_LEN];
static uint8_t slave_write_buf[I2C_MAX_LEN];
static uint8_t master_read_buf[I2C_MAX_LEN];
static uint8_t slave_read_buf[I2C_MAX_LEN];
static uint8_t packet_buf[I2C_MAX_LEN];

typedef struct {
    uint8_t version;
    uint16_t length;
    uint16_t id;
    //bit15 - more fragments
    //other bits - fragment offset in bytes of the data only
    uint16_t ffragment_offset;
    uint8_t src;
} __attribute__((__packed__)) Header;

#define MAX_DATA_LEN (I2C_MAX_LEN-sizeof(Header))

typedef struct {
    Header header;
    uint8_t data[MAX_DATA_LEN];
} __attribute__((__packed__)) Packet;

static uint8_t src_address;
static uint16_t id = 0;
volatile static uint8_t new_packet;
static uint8_t new_packet_length;
static void iterate_read_buf(void);

static void i2c_master_slave_callback(
        int callback_type,
        int length,
        int unused __attribute__ ((unused)),
        void* callback_args __attribute ((unused))) {

    if(callback_type == CB_SLAVE_WRITE) {
        memcpy(packet_buf, slave_write_buf, length);
        new_packet_length = length;
        new_packet = 1;
    } else if (callback_type == CB_SLAVE_READ_COMPLETE) {
        iterate_read_buf();
    }
}

void message_init(uint8_t src) {
    i2c_master_slave_set_slave_write_buffer(slave_write_buf, I2C_MAX_LEN);
    i2c_master_slave_set_master_write_buffer(master_write_buf, I2C_MAX_LEN);
    i2c_master_slave_set_slave_read_buffer(slave_read_buf, I2C_MAX_LEN);
    //i2c_master_slave_set_master_read_buffer(master_read_buf, BUFFER_SIZE);
    i2c_master_slave_set_callback(i2c_master_slave_callback, NULL);
    i2c_master_slave_set_slave_address(src);
    src_address = src;
    gpio_enable_output(2);
    gpio_set(2);
}

static uint16_t htons(uint16_t in) {
    return (((in & 0x00FF) << 8) | ((in & 0xFF00) >> 8));
}

static uint32_t readToSend;
static uint32_t readLen;
static Packet readPacket;
static uint8_t* readData;

static void iterate_read_buf() {

    if(readToSend > 0) {
        //set more fragments bit
        readPacket.header.ffragment_offset = 0;
        uint8_t morePackets = 0;
        uint16_t offset = 0;

        //calculate moreFragments
        morePackets = (readToSend > MAX_DATA_LEN);
        //calculate offset
        offset = (readLen-readToSend);

        //set more fragments bit
        readPacket.header.ffragment_offset = ((morePackets & 0x01) << 15);

        //set the fragment offset
        readPacket.header.ffragment_offset = htons((readPacket.header.ffragment_offset | offset));

        //set the data field
        //if there are more packets write the whole packet
        if(morePackets) {
            memcpy(readPacket.data,
                    readData+offset,
                    MAX_DATA_LEN);
            gpio_set(2);
        } else {
            //if not just send the remainder of the data
            memcpy(readPacket.data,
                    readData+offset,
                    readToSend);
            gpio_clear(2);
        }

        //copy the packet into the send buffer
        memcpy(slave_read_buf,&readPacket,I2C_MAX_LEN);

        //send the packet in syncronous mode
        if(morePackets) {
            readToSend -= MAX_DATA_LEN;
        } else {
            readToSend = 0;
        }

    } else {
        readToSend = readLen;
        iterate_read_buf();
    }
}

void message_set_read_buffer(uint8_t* data, uint32_t len) {
    //essentially call message_send on the buff but only
    //iterate after someone has read the buffer.
    //after the buffer has been read restart it from
    //the beginning
    i2c_master_slave_listen();
    id++;
    readLen = len;
    readToSend = readLen;
    readData = data;

    //calculate the number of packets we will have to send
    uint16_t numPackets;
    if(len % MAX_DATA_LEN) {
        numPackets = (len/MAX_DATA_LEN) + 1;
    } else {
        numPackets = (len/MAX_DATA_LEN);
    }

    //set version
    readPacket.header.version = 0x01;
    //set the source
    readPacket.header.src = src_address;
    readPacket.header.id = htons(id);

    //set the total length
    readPacket.header.length = htons((numPackets*sizeof(Header))+len);

    iterate_read_buf();
}


//synchronous send call
uint32_t message_send(uint8_t dest, uint8_t* data, uint32_t len) {
    id++;
    Packet p;
    uint32_t toSend = len;

    //calculate the number of packets we will have to send
    uint16_t numPackets;
    if(len % MAX_DATA_LEN) {
        numPackets = (len/MAX_DATA_LEN) + 1;
    } else {
        numPackets = (len/MAX_DATA_LEN);
    }

    //set version
    p.header.version = 0x01;
    //set the source
    p.header.src = src_address;
    p.header.id = htons(id);

    //set the total length
    p.header.length = htons((numPackets*sizeof(Header))+len);

    while(toSend > 0) {
        //set more fragments bit
        p.header.ffragment_offset = 0;
        uint8_t morePackets = 0;
        uint16_t offset = 0;

        //calculate moreFragments
        morePackets = (toSend > MAX_DATA_LEN);
        //calculate offset
        offset = (len-toSend);

        //set more fragments bit
        p.header.ffragment_offset = ((morePackets & 0x01) << 15);

        //set the fragment offset
        p.header.ffragment_offset = htons((p.header.ffragment_offset | offset));

        //set the data field
        //if there are more packets write the whole packet
        if(morePackets) {
            memcpy(p.data,
                    data+offset,
                    MAX_DATA_LEN);
        } else {
            //if not just send the remainder of the data
            memcpy(p.data,
                    data+offset,
                    toSend);
        }

        //copy the packet into the send buffer
        memcpy(master_write_buf,&p,I2C_MAX_LEN);

        //send the packet in syncronous mode
        if(morePackets) {
            i2c_master_slave_write_sync(dest,I2C_MAX_LEN);
            i2c_master_slave_set_callback(i2c_master_slave_callback, NULL);
            toSend -= MAX_DATA_LEN;
        } else {
            i2c_master_slave_write_sync(dest,sizeof(Header)+toSend);
            i2c_master_slave_set_callback(i2c_master_slave_callback, NULL);
            toSend = 0;
        }
    }

    return len;
}

//blocking receive call
uint32_t message_recv(uint8_t* data, uint32_t len, uint8_t* src) {

    i2c_master_slave_listen();

    uint8_t done = 0;
    uint32_t lengthReceived = 0;
    uint16_t messageID;
    uint8_t messageSrc;

    //loop receiving packets until we get the whole datagram
    while(!done) {

        //wait and receive a packet
        new_packet = 0;
        while(!new_packet) {
            yield();
        }

        //a new packet is in the packet buf

        //copy the packet into a header struct
        Packet p;
        memcpy(&p,packet_buf,I2C_MAX_LEN);


        if(lengthReceived == 0) {
            //this is the first packet
            //save the messageID
            messageID = p.header.id;
            messageSrc = p.header.src;
        } else {
            //this is not the first packet
            //is this the same messageID?
            if(messageID == p.header.id && messageSrc == p.header.src) {
                //yes it is - proceed
            } else {
                //we should drop this packet
                continue;
            }
        }

        *src = p.header.src;

        //are there more fragments?
        uint8_t moreFragments = (htons(p.header.ffragment_offset) & 0x8000) >> 15;
        uint16_t fragmentOffset = (htons(p.header.ffragment_offset) & 0x7FFF);

        if(moreFragments) {
            //is there room to copy into the buffer?
            if(fragmentOffset + MAX_DATA_LEN > len) {
                //this is too long
                //just copy what we can and end
                uint16_t remainder = len - fragmentOffset;
                memcpy(data+fragmentOffset,p.data,remainder);
                lengthReceived += remainder;
                done = 1;
            } else {
                memcpy(data+fragmentOffset,p.data,MAX_DATA_LEN);
                lengthReceived += MAX_DATA_LEN;
            }
        } else {
            //is there room to copy into the buffer?
            if(fragmentOffset + (new_packet_length - sizeof(Header)) > len) {
                //this is too long
                //just copy what we can and end
                uint16_t remainder = len - fragmentOffset;
                memcpy(data+fragmentOffset,p.data,remainder);
                lengthReceived += remainder;
            } else {
                //copy the rest of the packet
                memcpy(data+fragmentOffset,p.data,(new_packet_length - sizeof(Header)));
                lengthReceived += new_packet_length - sizeof(Header);
            }

            //no more fragments end
            done = 1;
        }
    }

    return lengthReceived;
}

