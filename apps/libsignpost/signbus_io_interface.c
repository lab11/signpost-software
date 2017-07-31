#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "signbus_io_interface.h"
#include "port_signpost.h"

#pragma GCC diagnostic ignored "-Wstack-usage="

static uint8_t slave_write_buf[I2C_MAX_LEN];
static uint8_t packet_buf[I2C_MAX_LEN];

typedef struct __attribute__((packed)) signbus_network_flags {
    unsigned int is_fragment   : 1;
    unsigned int is_encrypted  : 1;
    unsigned int rsv_wire_bit5 : 1;
    unsigned int rsv_wire_bit4 : 1;
    unsigned int version       : 4;
} signbus_network_flags_t;
_Static_assert(sizeof(signbus_network_flags_t) == 1, "network flags size");

typedef struct __attribute__((packed)) signbus_network_header {
    union {
        uint8_t flags_storage;
        signbus_network_flags_t flags;
    };
    uint8_t src;
    uint16_t sequence_number;
    uint16_t length;
    uint16_t fragment_offset;
} signbus_network_header_t;
_Static_assert(sizeof(signbus_network_header_t) == 8, "network header size");

#define MAX_DATA_LEN (I2C_MAX_LEN-sizeof(signbus_network_header_t))

typedef struct {
    signbus_network_header_t header;
    uint8_t data[MAX_DATA_LEN];
} __attribute__((__packed__)) Packet;

typedef struct {
    bool new;
    uint8_t len;
} new_packet;

static uint8_t this_device_address;
static uint16_t sequence_number = 0;
static new_packet newpkt = { .new = false };

__attribute__((const))
static uint16_t htons(uint16_t in) {
    return (((in & 0x00FF) << 8) | ((in & 0xFF00) >> 8));
}


/***************************************************************************
 * Tock I2C Interface
 *
 * The underlying I2C mechanism imposes a 255 byte MTU.
 * On the sending side, this means we simply fragment until complete.
 * Receiving is a little more tricky. We pass a 255 byte buffer that we own to
 * the I2C driver which is always re-used to get individual messages from the
 * I2C bus. When an upper layer wishes to recieve a message, it passes a buffer
 * to copy into, which we copy I2C data into until we get a complete message.
 *
 ***************************************************************************/

// Internal helper that copies data from the I2C driver buffer to the packet
// buffer. Forward declaration here so callback can use it.
static int get_message(
        uint8_t* recv_buf,
        size_t recv_buflen,
        bool*   is_encrypted,
        uint8_t* src_address
        );

// Internal helper for supporting slave reads. Forward declaration here so
// callback can use it.
static void signbus_iterate_slave_read(void);

// State for an active async event
bool                    async_active = false;
uint8_t*                async_recv_buf;
size_t                  async_recv_buflen;
bool*                   async_encrypted;
uint8_t*                async_src_address;
signbus_app_callback_t* async_callback = NULL;

// flag to indicate if callback is for async operation
static bool async = false;

void signbus_io_slave_write_callback(int len_or_rc);
void signbus_io_slave_write_callback(int len_or_rc) {
    if(len_or_rc >= 0) {
        memcpy(packet_buf, slave_write_buf, len_or_rc);
        new_packet* packet = (new_packet*) &newpkt;
        packet->new = true;
        packet->len = len_or_rc;
        if (async_active) {
            get_message(async_recv_buf, async_recv_buflen, async_encrypted, async_src_address);
        }
    }
}


/// Initialization routine that sets up buffers used to read and write to
/// the underlying I2C device. Also sets the slave address for this device.
///
/// MUST be called before any other methods.
void signbus_io_init(uint8_t address) {
    this_device_address = address;
    port_signpost_init(address);
}


// synchronous send call
int signbus_io_send(uint8_t dest, bool encrypted, uint8_t* data, size_t len) {
    SIGNBUS_DEBUG("dest %02x data %packet len %d\n", dest, data, len);

    sequence_number++;
    Packet packet = {0};
    size_t toSend = len;

    //calculate the number of packets we will have to send
    uint16_t numPackets;
    if(len % MAX_DATA_LEN) {
        numPackets = (len/MAX_DATA_LEN) + 1;
    } else {
        numPackets = (len/MAX_DATA_LEN);
    }

    //set encrypted
    packet.header.flags.is_encrypted = encrypted;
    //set version
    packet.header.flags.version = 0x01;
    //set the source
    packet.header.src = this_device_address;
    packet.header.sequence_number = htons(sequence_number);

    //set the total length
    packet.header.length = htons((numPackets*sizeof(signbus_network_header_t))+len);

    while(toSend > 0) {
        uint8_t morePackets = 0;
        uint16_t offset = 0;
        int rc;

        //calculate moreFragments
        morePackets = (toSend > MAX_DATA_LEN);
        //calculate offset
        offset = (len-toSend);

        //set more fragments bit
        packet.header.flags.is_fragment = morePackets;

        //set the fragment offset
        packet.header.fragment_offset = htons(offset);

        //set the data field
        //if there are more packets write the whole packet
        if(morePackets) {
            memcpy(packet.data,
                    data+offset,
                    MAX_DATA_LEN);
        } else {
            //if not just send the remainder of the data
            memcpy(packet.data,
                    data+offset,
                    toSend);
        }

        //send the packet
        if(morePackets) {
            rc = port_signpost_i2c_master_write(dest,(uint8_t *) &packet,I2C_MAX_LEN);

            if (rc < 0) return rc;

            toSend -= MAX_DATA_LEN;
        } else {
            //SIGNBUS_DEBUG_DUMP_BUF(&packet, sizeof(signbus_network_header_t)+toSend);

            rc = port_signpost_i2c_master_write(dest, (uint8_t *) &packet,sizeof(signbus_network_header_t)+toSend);

            if (rc < 0) return rc;

            toSend = 0;
        }
    }

    SIGNBUS_DEBUG("dest %02x data %packet len %d -- COMPLETE\n", dest, data, len);
    return len;
}

// get_message is called either from a synchronous context, or from the
// callback path of an async request. In either case, this method can
// safely call blocking methods until it is ready to either return or
// call up the callback chain.
//
// This function will return the number of bytes received or < 0 for rcor.
// For async invocation, the return value is passed as the callback argument.
static int get_message(uint8_t* data, size_t len, bool* encrypted, uint8_t* src) {
    uint8_t done = 0;
    size_t lengthReceived = 0;
    uint16_t message_sequence_number;
    uint8_t message_source_address;

    // Mark async as inactive so this call stack can block
    async_active = false;

    //loop receiving packets until we get the whole datagram
    while(!done) {
        //wait and receive a packet
        if (!newpkt.new) {
            port_signpost_wait_for(&newpkt.new);
        }
        newpkt.new = 0;

        //a new packet is in the packet buf

        //copy the packet into a header struct
        Packet packet;
        memcpy(&packet,packet_buf,I2C_MAX_LEN);
        if(lengthReceived == 0) {
            //this is the first packet
            //save the message_sequence_number
            message_sequence_number = packet.header.sequence_number;
            message_source_address = packet.header.src;
            *encrypted = packet.header.flags.is_encrypted;
        } else {
            //this is not the first packet
            //is this the same message_sequence_number?
            if(message_sequence_number == packet.header.sequence_number
                    && message_source_address == packet.header.src) {
                //yes it is - proceed
            } else {
                //we should drop this packet
                continue;
            }
        }

        *src = packet.header.src;

        //are there more fragments?
        uint8_t moreFragments = packet.header.flags.is_fragment;
        uint16_t fragmentOffset = htons(packet.header.fragment_offset);
        if(moreFragments) {
            //is there room to copy into the buffer?
            if(fragmentOffset + MAX_DATA_LEN > len) {
                //this is too long
                //just copy what we can and end
                uint16_t remainder = len - fragmentOffset;
                memcpy(data+fragmentOffset,packet.data,remainder);
                lengthReceived += remainder;
                done = 1;
            } else {
                memcpy(data+fragmentOffset,packet.data,MAX_DATA_LEN);
                lengthReceived += MAX_DATA_LEN;
            }
        } else {
            //is there room to copy into the buffer?
            if(fragmentOffset + (newpkt.len - sizeof(signbus_network_header_t)) > len) {
                //this is too long
                //just copy what we can and end
                uint16_t remainder = len - fragmentOffset;
                memcpy(data+fragmentOffset,packet.data,remainder);
                lengthReceived += remainder;
            } else {
                //copy the rest of the packet
                memcpy(data+fragmentOffset,packet.data,(newpkt.len - sizeof(signbus_network_header_t)));
                lengthReceived += newpkt.len - sizeof(signbus_network_header_t);
            }

            //no more fragments end
            done = 1;
        }
    }

    SIGNBUS_DEBUG_DUMP_BUF(data, lengthReceived);

    if (async_callback != NULL) {
        // allow recursion
        signbus_app_callback_t* temp = async_callback;
        async_callback = NULL;
        temp(lengthReceived);
    }

    return lengthReceived;
}

// blocking receive call
int signbus_io_recv(
        size_t recv_buflen,
        uint8_t* recv_buf,
        bool*    encrypted,
        uint8_t* src_address
        ) {

    async = false;

    int rc;
    rc = port_signpost_i2c_slave_listen(signbus_io_slave_write_callback, slave_write_buf, I2C_MAX_LEN);
    if (rc < 0) return rc;

    return get_message(recv_buf, recv_buflen, encrypted, src_address);
}

// async receive call
int signbus_io_recv_async(
        signbus_app_callback_t callback,
        size_t recv_buflen,
        uint8_t* recv_buf,
        bool*    encrypted,
        uint8_t* src
        ) {

    async_callback = callback;
    async_recv_buflen = recv_buflen;
    async_recv_buf = recv_buf;
    async_src_address = src;
    async_encrypted = encrypted;
    async_active = true;

    return port_signpost_i2c_slave_listen(signbus_io_slave_write_callback, slave_write_buf, I2C_MAX_LEN);
}



/******************************************************************************
 * Slave Read Section
 *
 * This is the code that's responsible for supporting I2C reads.
 * Currently, reads use the same Network Layer as the rest of signbus, but do
 * not follow any of the higher layers of the protocol.
 ******************************************************************************/

static uint32_t slave_read_data_bytes_remaining = 0;
static uint32_t slave_read_data_len = 0;
static Packet readPacket = {0};
static uint8_t* slave_read_data = NULL;
static signbus_app_callback_t* slave_read_callback = NULL;

static void signbus_iterate_slave_read(void) {

    // check if there is still data to send
    if (slave_read_data_bytes_remaining > 0) {


        // this message is a fragment if the entire data buffer cannot fit
        bool is_fragment = (slave_read_data_bytes_remaining > MAX_DATA_LEN);

        // calculate current offset into the data buffer
        uint16_t offset = slave_read_data_len - slave_read_data_bytes_remaining;

        // set packet header values
        readPacket.header.flags.is_fragment = is_fragment;
        readPacket.header.fragment_offset = htons(offset);

        // set the packet data field
        uint32_t data_len = slave_read_data_bytes_remaining;
        if (is_fragment) {
            // we cannot fit the whole data buffer. Copy as much as we can
            data_len = MAX_DATA_LEN;
        }
        memcpy(readPacket.data, &slave_read_data[offset], data_len);

        // update bytes remaining
        slave_read_data_bytes_remaining -= data_len;

        // setup slave read
        port_signpost_i2c_slave_read_setup((uint8_t *) &readPacket, I2C_MAX_LEN);

    } else {
        // all provided data has been sent! Do something about it
        if (slave_read_callback == NULL && slave_read_data_len > 0) {
            // do repitition of provided buffer... for legacy reasons
            slave_read_data_bytes_remaining = slave_read_data_len;
            signbus_iterate_slave_read();

        } else {
            // perform the callback!
            slave_read_callback(0);
        }
    }
}

// provide data for a slave read
int signbus_io_set_read_buffer (uint8_t* data, uint32_t len) {
    // reads work just like writing messages, but only iterate through
    // fragments after someone has read the buffer. A callback will be sent
    // after the entire data buffer has been read if one has been provided.
    // It's up to the application layers to ensure that the master is reading
    // the correct number of bytes from the slave. For legacy code reasons,
    // after the buffer has been read, if no callback has been provided we set
    // the next read to start from the beginning of the buffer again. It's
    // expected that the application layer will call signbus_io_set_read_buffer
    // from the callback to provide new data

    // listen for i2c messages asynchronously
    int rc = port_signpost_i2c_slave_listen(signbus_io_slave_write_callback, slave_write_buf, I2C_MAX_LEN);
    if (rc < 0) {
        return rc;
    }

    // sequence number is incremented once per data
    sequence_number++;

    // keep track of data and length
    slave_read_data_len = len;
    slave_read_data = data;

    // still need to send entire buffer
    slave_read_data_bytes_remaining = len;

    //calculate the number of packets we will have to send
    uint16_t numPackets;
    if(len % MAX_DATA_LEN) {
        numPackets = (len/MAX_DATA_LEN) + 1;
    } else {
        numPackets = (len/MAX_DATA_LEN);
    }

    // setup metadata for packets to be read
    readPacket.header.flags.version = 0x01;
    readPacket.header.src = this_device_address;
    readPacket.header.sequence_number = htons(sequence_number);

    //set the total length
    readPacket.header.length = htons((numPackets*sizeof(signbus_network_header_t))+len);

    // actually set up the fragmented read packet
    signbus_iterate_slave_read();
    return SB_PORT_SUCCESS;
}

// provide callback to be performed when the slave read has completed all data
// provided (possibly across multiple packets). Callback is not cleared after
// use
void signbus_io_set_read_callback (signbus_app_callback_t* callback) {
    slave_read_callback = callback;
}

