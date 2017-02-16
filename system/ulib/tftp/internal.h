#pragma once

#include <stddef.h>
#include <stdint.h>

#include "tftp/tftp.h"

#define OPCODE_RRQ 1
#define OPCODE_RWQ 2
#define OPCODE_DATA 3
#define OPCODE_ACK 4
#define OPCODE_ERROR 5
#define OPCODE_OACK 6
#define OPCODE_OERROR 8

typedef struct tftp_msg_t {
    uint16_t opcode;
    char data[0];
} tftp_msg;

typedef struct tftp_data_msg_t {
    uint16_t opcode;
    uint16_t block;
    uint8_t data[0];
} tftp_data_msg;

#define BLOCKSIZE_OPTION 0x01  // RFC 2348
#define TIMEOUT_OPTION 0x02    // RFC 2349
#define FILESIZE_OPTION 0x04   // RFC 2349
#define WINDOWSIZE_OPTION 0x08 // RFC 7440

typedef struct tftp_options_t {
    // Maximum filename really is 505 including \0
    // max request size (512) - opcode (2) - shortest mode (4) - null (1)
    char filename[512];
    tftp_mode mode;
    uint8_t requested;

    uint16_t block_size;
    uint8_t timeout;
    uint32_t file_size;

    uint32_t window_size;
} tftp_options;

/**
 Sender
 NONE -(tftp_generate_write_request)-> WRITE_REQUESTED
 WRITE_REQUESTED -(tftp_receive = OPCODE_OACK)-> TRANSMITTING
 WRITE_REQUESTED -(tftp_receive = OPCODE_ACK)-> TRANSMITTING
 WRITE_REQUESTED -(tftp_receive = OPCODE_ERROR)-> ERROR
 TRANSMITTING -(tftp_receive = OPCODE_ACK)-> TRANSMITTING
 TRANSMITTING -(tftp_receive = OPCODE_ERROR)-> ERROR
 TRANSMITTING -(last packet)-> LAST_PACKET
 LAST_PACKET -(tftp_receive = OPCODE_ERROR)-> ERROR
 LAST_PACKET -(tftp_receive = OPCODE_ACK last packet)-> COMPLETED
 LAST_PACKET -(tftp_receive = OPCODE_ACK not last packet)-> TRANSMITTING
 COMPLETED -(tftp_receive)-> ERROR

 Receiver
 NONE -(tftp_receive = OPCODE_RWQ)-> WRITE_REQUESTED
 NONE -(tftp_receive != OPCODE_RWQ)-> ERROR
 WRITE_REQUESTED -(tftp_receive = OPCODE_DATA) -> TRANSMITTING
 WRITE_REQUESTED -(tftp_receive != OPCODE_DATA) -> ERROR
 TRANSMITTING -(tftp_receive = OPCODE_DATA)-> TRANSMITTING
 TRANSMITTING -(tftp_receive != OPCODE_DATA)-> ERROR
 TRANSMITTING -(last packet)-> COMPLETED
 COMPLETED -(tftp_receive)-> ERROR
**/

typedef enum {
    NONE,
    WRITE_REQUESTED,
    TRANSMITTING,
    LAST_PACKET,
    ERROR,
    COMPLETED,
} tftp_state;

// TODO add a state so time out can be handled properly as well as unexpected traffic
struct tftp_session_t {
    tftp_options options;
    tftp_state state;
    char* data;
    size_t offset;

    uint32_t block_number;

    // "Negotiated" values
    size_t file_size;
    tftp_mode mode;
    uint32_t window_index;
    uint32_t window_size;
    uint16_t block_size;
    uint8_t timeout;
};

size_t next_option(char* buffer, size_t len, char** option, char** value);

void print_hex(uint8_t* buf, size_t len);
