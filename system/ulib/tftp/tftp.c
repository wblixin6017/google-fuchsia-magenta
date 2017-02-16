#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

// TODO
// 1) How to we tell consumer to move forward when sending with window > 1?
//    - The tricky part being that tftp_receive will need to be called multiple times even if nothing is received...
// 2) How do we get data access or allocated depending on direction?

// RRQ    ->
//        <- DATA or OACK or ERROR
// ACK(0) -> (to confirm reception of OACK)
// ERROR  -> (on OACK with non requested options)
//        <- DATA(1)
// ACK(1) ->

// RWQ     ->
//         <- ACK or OACK or ERROR
// DATA(1) ->
// ERROR   -> (on OACK with non requested options)
//        <- DATA(2)
// ACK(2) ->

static const char* kNetascii = "NETASCII";
static const char* kOctet = "OCTET";
static const char* kMail = "MAIL";
static const char* kTsize = "TSIZE";
static const char* kBlkSize = "BLKSIZE";
static const char* kTimeout = "TIMEOUT";
static const char* kWindowSize = "WINDOWSIZE";

#define APPEND_OPTION_NAME(body, left, name) \
    do {                                     \
        size_t offset = strlen(name);        \
        memcpy(body, name, offset);          \
        offset++;                            \
        body += offset;                      \
        left -= offset;                      \
    } while (0)

#define APPEND_OPTION(body, left, name, fmt, value)    \
    do {                                               \
        size_t offset = strlen(name);                  \
        memcpy(body, name, offset);                    \
        offset++;                                      \
        body += offset;                                \
        left -= offset;                                \
        offset = snprintf(body, left - 1, fmt, value); \
        offset++;                                      \
        body += offset;                                \
        left -= offset;                                \
    } while (0)

#define OPCODE(msg, value)                \
    do {                                  \
        char* dest = (void*)&msg->opcode; \
        *dest++ = (value >> 8) & 0xFF;    \
        *dest++ = (value)&0xFF;           \
    } while (0)

#define TRANSMIT_MORE 1
#define TRANSMIT_WAIT_ON_ACK 2

void print_hex(uint8_t* buf, size_t len) {
#if 0
    for (size_t i = 0; i < len; i++) {
        fprintf(stdout, "%02x ", buf[i]);
        if (i % 16 == 15)
            fprintf(stdout, "\n");
    }
    fprintf(stdout, "\n");
#endif
}

uint32_t _transmit(tftp_session* session, tftp_data_msg* ack, size_t* outlen) {
    session->offset = (session->block_number + session->window_index) * session->block_size;
    *outlen = 0;
    if (session->offset < session->file_size) {
        session->window_index++;
        OPCODE(ack, OPCODE_DATA);
        ack->block = session->block_number + session->window_index;
        size_t len = session->offset + session->block_size > session->file_size ? session->file_size - session->offset : session->block_size;
        fprintf(stdout, " -> Copying block #%d (size:%zu/%d) from %zu/%zu [%d/%d]\n", session->block_number + session->window_index, len, session->block_size, session->offset, session->file_size, session->window_index, session->window_size);
        memcpy(ack->data, session->data + session->offset, len);
        *outlen = 4 + len;
        if (session->window_index < session->window_size) {
            fprintf(stdout, " -> TRANSMIT_MORE(%d < %d)\n", session->window_index, session->window_size);
            return TRANSMIT_MORE;
        } else {
            fprintf(stdout, " -> TRANSMIT_WAIT_ON_ACK(%d >= %d)\n", session->window_index, session->window_size);
            session->block_number += session->window_size;
            session->window_index = 0;
            return TRANSMIT_WAIT_ON_ACK;
        }
    } else {
        fprintf(stdout, " -> TRANSMIT_WAIT_ON_ACK(completed)\n");
        return TRANSMIT_WAIT_ON_ACK;
    }
}

uint32_t
tftp_init(tftp_session** session, void* buffer, size_t size) {
    if (size < sizeof(tftp_session)) {
        fprintf(stderr, "Not enough memory: %ld expected %ld\n", size, sizeof(tftp_session));
        return -1;
    }
    *session = buffer;
    tftp_session* s = *session;
    memset(s, 0, sizeof(tftp_session));
    s->state = NONE;
    s->data = NULL;
    s->offset = 0;
    s->block_number = 0;
    s->window_index = 0;

    // Sensible default values
    s->file_size = s->options.file_size = 0;
    s->window_size = s->options.window_size = 1;
    s->block_size = s->options.block_size = 512;
    s->timeout = s->options.timeout = 1;
    s->mode = s->options.mode = MODE_OCTET;

    return NO_ERROR;
}

uint32_t tftp_generate_write_request(tftp_session* session,
                                     const char* filename,
                                     tftp_mode mode,
                                     void** data,
                                     size_t datalen,
                                     size_t block_size,
                                     uint8_t timeout,
                                     uint8_t window_size,
                                     void** outgoing,
                                     size_t* outlen,
                                     uint32_t* timeout_ms,
                                     tftp_send_message send_message,
                                     void* cookie) {
    if (*outlen < 2) {
        return -1;
    }

    if (!*data) {
        return -1;
    }

    tftp_msg* ack = *outgoing;
    OPCODE(ack, OPCODE_RWQ);
    char* body = ack->data;
    memset(body, 0, *outlen - 2);
    size_t left = *outlen - 2;
    // Maximum mode is 8 bytes, + 2 bytes for opcode, + 2 null terminators
    if (strlen(filename) > *outlen - 12) {
        return -1;
    }
    strncpy(session->options.filename, filename, 512);
    memcpy(body, filename, strlen(filename));
    body += strlen(filename) + 1;
    left -= strlen(filename) + 1;
    switch (mode) {
    case MODE_NETASCII:
        APPEND_OPTION_NAME(body, left, kNetascii);
        break;
    case MODE_OCTET:
        APPEND_OPTION_NAME(body, left, kOctet);
        break;
    case MODE_MAIL:
        APPEND_OPTION_NAME(body, left, kMail);
        break;
    default:
        return -1;
    }
    session->options.mode = mode;

    if (left < 12) {
        return -1;
    }
    APPEND_OPTION(body, left, kTsize, "%ld", datalen);
    session->data = (char*)*data;
    session->file_size = datalen;

    if (block_size > 0) {
        if (left < 14) {
            return -1;
        }
        APPEND_OPTION(body, left, kBlkSize, "%ld", block_size);
        session->options.block_size = block_size;
        session->options.requested |= BLOCKSIZE_OPTION;
    }

    if (timeout > 0) {
        if (left < 14) {
            return -1;
        }
        APPEND_OPTION(body, left, kTimeout, "%d", timeout);
        session->options.timeout = timeout;
        session->options.requested |= TIMEOUT_OPTION;
    }

    if (window_size > 1) {
        if (left < 16) {
            return -1;
        }
        APPEND_OPTION(body, left, kWindowSize, "%d", window_size);
        session->options.window_size = window_size;
        session->options.requested |= WINDOWSIZE_OPTION;
    }

    *outlen = *outlen - left;
    // Nothing has been negotiated yet so use default
    *timeout_ms = 1000 * session->timeout;

    if (send_message(*outgoing, *outlen, cookie) <= 0) {
        session->state = ERROR;
        return TRANSFER_ERROR;
    }

    session->state = WRITE_REQUESTED;
    return NO_ERROR;
}

uint32_t tftp_receive(tftp_session* session,
                      void* incoming,
                      size_t inlen,
                      void** outgoing,
                      size_t* outlen,
                      uint32_t* timeout_ms,
                      tftp_open_file open_file,
                      tftp_send_message send_message,
                      void* cookie) {
    tftp_msg* msg = incoming;
    tftp_msg* ack = *outgoing;

    // Decode opcode
    char* source = (void*)incoming;
    uint16_t opcode = 0;
    opcode |= (*source++ & 0xFF) << 8;
    opcode |= (*source++ & 0xFF);

    *timeout_ms = 1000 * session->timeout;

    switch (opcode) {
    case OPCODE_RRQ:
    case OPCODE_RWQ:
        if (session->state != NONE) {
            fprintf(stderr, "Invalid state transition %d -> %d\n", session->state, WRITE_REQUESTED);
            OPCODE(ack, OPCODE_ERROR);
            *outlen = 2;
            send_message(*outgoing, *outlen, cookie);
            return TRANSFER_ERROR;
        }
        // opcode, filename, 0, mode, 0, opt1, 0, value1 ... optN, 0, valueN, 0
        // Max length is 512 no matter
        if (inlen > 512) {
            fprintf(stderr, "Read/Write request is too large\n");
            OPCODE(ack, OPCODE_ERROR);
            *outlen = 2;
            send_message(*outgoing, *outlen, cookie);
            session->state = ERROR;
            return TRANSFER_ERROR;
        }
        // Skip opcode
        size_t left = inlen - 2;
        char* cur = msg->data;
        char *option, *value;
        // filename, 0, mode, 0 can be interpreted like option, 0, value, 0
        size_t offset = next_option(cur, left, &option, &value);
        if (!offset) {
            OPCODE(ack, OPCODE_ERROR);
            *outlen = 2;
            send_message(*outgoing, *outlen, cookie);
            session->state = ERROR;
            return TRANSFER_ERROR;
        }

        fprintf(stderr, "filename = '%s', mode = '%s'\n", option, value);

        strncpy(session->options.filename, option, 512);
        char* mode = value;
        if (!strncasecmp(mode, kNetascii, strlen(kNetascii))) {
            session->options.mode = MODE_NETASCII;
        } else if (!strncasecmp(mode, kOctet, strlen(kOctet))) {
            session->options.mode = MODE_OCTET;
        } else if (!strncasecmp(mode, kMail, strlen(kMail))) {
            session->options.mode = MODE_MAIL;
        } else {
            fprintf(stderr, "Unknown read/write request mode\n");
            OPCODE(ack, OPCODE_ERROR);
            *outlen = 2;
            send_message(*outgoing, *outlen, cookie);
            session->state = ERROR;
            return TRANSFER_ERROR;
        }

        cur += offset;
        while (offset > 0 && left > 0) {
            offset = next_option(cur, left, &option, &value);
            if (!offset) {
                OPCODE(ack, OPCODE_ERROR);
                *outlen = 2;
                send_message(*outgoing, *outlen, cookie);
                session->state = ERROR;
                return TRANSFER_ERROR;
            }

            if (!strncasecmp(option, kBlkSize, strlen(kBlkSize))) { // RFC 2348
                // Valid values range between "8" and "65464" octets, inclusive
                unsigned long val = strtol(value, (char**)NULL, 10);
                if (val < 8 || val > 65464) {
                    // Invalid
                }
                // With an MTU of 1500, shouldn't be more than 1428
                session->options.block_size = val;
                session->options.requested |= BLOCKSIZE_OPTION;
            } else if (!strncasecmp(option, kTimeout, strlen(kTimeout))) { // RFC 2349
                // Valid values range between "1" and "255" seconds inclusive.
                unsigned long val = strtol(value, (char**)NULL, 10);
                if (val < 1 || val > 255) {
                    // Invalid
                }
                session->options.timeout = val;
                session->options.requested |= TIMEOUT_OPTION;
            } else if (!strncasecmp(option, kTsize, strlen(kTsize))) { // RFC 2349
                session->options.file_size = strtol(value, (char**)NULL, 10);
                if (msg->opcode == OPCODE_RRQ && session->options.file_size != 0) {
                    // On read request, it should be 0.
                }
                session->options.requested |= FILESIZE_OPTION;
            } else if (!strncasecmp(option, kWindowSize, strlen(kWindowSize))) { // RFC 7440
                // The valid values range MUST be between 1 and 65535 blocks, inclusive.
                unsigned long val = strtol(value, (char**)NULL, 10);
                if (val < 1 || val > 65464) {
                    // Invalid
                }

                session->options.window_size = strtol(value, (char**)NULL, 10);
                session->options.requested |= WINDOWSIZE_OPTION;
            } else {
                // Options which the server does not support should be omitted from the
                // OACK; they should not cause an ERROR packet to be generated.
            }

            cur += offset;
            left -= offset;
        }

        char* body = ack->data;
        memset(body, 0, *outlen - 2);
        left = *outlen - 2;

        if (msg->opcode == OPCODE_RRQ) {
            if (session->options.requested) {
                // TODO(jpoichet)
                if (session->options.requested & FILESIZE_OPTION) {
                    // Need to give the size back
                }
                OPCODE(ack, OPCODE_OACK);
            } else {
                OPCODE(ack, OPCODE_DATA);
            }
        } else {
            if (session->options.requested) {
                OPCODE(ack, OPCODE_OACK);
                if (session->options.requested & BLOCKSIZE_OPTION) {
                    // TODO(jpoichet) Make sure this block size is possible. Need API upwards to request allocation of block size * window size memory
                    APPEND_OPTION(body, left, kBlkSize, "%d", session->options.block_size);
                    session->block_size = session->options.block_size;
                }
                if (session->options.requested & TIMEOUT_OPTION) {
                    // TODO(jpoichet) Make sure this timeout is possible. Need API upwards to request allocation of block size * window size memory
                    APPEND_OPTION(body, left, kTimeout, "%d", session->options.timeout);
                    session->timeout = session->options.timeout;
                    *timeout_ms = 1000 * session->timeout;
                }
                if (session->options.requested & WINDOWSIZE_OPTION) {
                    APPEND_OPTION(body, left, kWindowSize, "%d", session->options.window_size);
                    session->window_size = session->options.window_size;
                }
                if (!open_file || open_file(session->options.filename, session->options.file_size, (void**)&session->data, cookie)) {
                    fprintf(stderr, "Could not allocate buffer on write request\n");
                    OPCODE(ack, OPCODE_ERROR);
                }
                session->file_size = session->options.file_size;
                // TODO notify library client of the size
                *outlen = *outlen - left;
                if (send_message(*outgoing, *outlen, cookie) <= 0) {
                    session->state = ERROR;
                    return TRANSFER_ERROR;
                }
                session->state = WRITE_REQUESTED;
            } else {
                OPCODE(ack, OPCODE_ACK);
                *outlen = 2;
                if (send_message(*outgoing, *outlen, cookie) <= 0) {
                    session->state = ERROR;
                    return TRANSFER_ERROR;
                }
                session->state = WRITE_REQUESTED;
            }
        }

        fprintf(stderr, "Read/Write Request Parsed\n");
        fprintf(stderr, "Options requested: %08x\n", session->options.requested);
        fprintf(stderr, "    Block Size : %d\n", session->options.block_size);
        fprintf(stderr, "    Timeout    : %d\n", session->options.timeout);
        fprintf(stderr, "    File Size  : %d\n", session->options.file_size);
        fprintf(stderr, "    Window Size: %d\n", session->options.window_size);

        fprintf(stderr, "Using options\n");
        fprintf(stderr, "    Block Size : %d\n", session->block_size);
        fprintf(stderr, "    Timeout    : %d\n", session->timeout);
        fprintf(stderr, "    File Size  : %zu\n", session->file_size);
        fprintf(stderr, "    Window Size: %d\n", session->window_size);

        return NO_ERROR;
    case OPCODE_DATA: {
        switch (session->state) {
        case WRITE_REQUESTED:
        case TRANSMITTING:
            session->state = TRANSMITTING;
            break;
        case NONE:
        case LAST_PACKET:
        case ERROR:
        case COMPLETED:
        default:
            OPCODE(ack, OPCODE_ERROR);
            *outlen = 2;
            send_message(*outgoing, *outlen, cookie);
            return TRANSFER_ERROR;
        }

        tftp_data_msg* data = (void*)msg;
        tftp_data_msg* ack_data = (void*)ack;
        fprintf(stdout, " <- Block %u (Last = %u, Offset = %d, Size = %ld, Left = %ld)\n", data->block, session->block_number,
                session->block_number * session->block_size, session->file_size,
                session->file_size - session->block_number * session->block_size);
        if (data->block == session->block_number + 1) {
            fprintf(stdout, "Advancing normally + 1\n");
            print_hex(data->data, inlen - 4);
            memcpy(session->data + session->block_number * session->block_size, data->data, inlen - 4);
            session->block_number++;
            session->window_index++;

        } else {
            // Force sending a ACK with the last block_number we received
            fprintf(stdout, "Skipped %d %d\n", data->block, session->block_number);
            session->window_index = session->window_size;
        }

        if (session->window_index == session->window_size || session->block_number * session->block_size >= session->file_size) {
            fprintf(stdout, " -> Ack %d\n", session->block_number);
            OPCODE(ack_data, OPCODE_ACK);
            ack_data->block = session->block_number;
            session->window_index = 0;
            *outlen = 4;
            send_message(*outgoing, *outlen, cookie);
            if (session->block_number * session->block_size >= session->file_size) {
                *outlen = 0;
                return TRANSFER_COMPLETED;
            }
        }
        return NO_ERROR;
    };
    case OPCODE_ACK: {
        switch (session->state) {
        case WRITE_REQUESTED:
        case TRANSMITTING:
            session->state = TRANSMITTING;
            break;
        case NONE:
        case LAST_PACKET:
        case ERROR:
        case COMPLETED:
        default:
            OPCODE(ack, OPCODE_ERROR);
            *outlen = 2;
            send_message(*outgoing, *outlen, cookie);
            return TRANSFER_ERROR;
        }
        // Need to move forward in data and send it
        tftp_data_msg* msg_data = (void*)msg;
        tftp_data_msg* ack_data = (void*)ack;

        fprintf(stdout, " <- Ack %d\n", msg_data->block);
        session->block_number = msg_data->block;

        if ((session->block_number + session->window_index) * session->block_size >= session->file_size) {
            *outlen = 0;
            return TRANSFER_COMPLETED;
        }

        uint32_t ret = TRANSMIT_MORE;
        while (ret == TRANSMIT_MORE) {
            ret = _transmit(session, ack_data, outlen);
            if (*outlen > 0) {
                send_message(*outgoing, *outlen, cookie);
            }
        }
        return NO_ERROR;
    };
    case OPCODE_ERROR: {
        fprintf(stdout, "Transfer Error\n");
        session->state = ERROR;
        return TRANSFER_ERROR;
    };
    case OPCODE_OACK: {
        fprintf(stdout, "Option Ack\n");
        switch (session->state) {
        case WRITE_REQUESTED:
            session->state = TRANSMITTING;
            break;
        case TRANSMITTING:
        case NONE:
        case LAST_PACKET:
        case ERROR:
        case COMPLETED:
        default:
            OPCODE(ack, OPCODE_ERROR);
            *outlen = 2;
            send_message(*outgoing, *outlen, cookie);
            return TRANSFER_ERROR;
        }

        size_t left = inlen - 2;
        char* cur = msg->data;
        size_t offset;
        char *option, *value;

        session->mode = session->options.mode;
        if (session->options.requested & BLOCKSIZE_OPTION) {
            session->block_size = session->options.block_size;
        }
        if (session->options.requested & TIMEOUT_OPTION) {
            session->timeout = session->options.timeout;
        }
        if (session->options.requested & WINDOWSIZE_OPTION) {
            session->window_size = session->options.window_size;
        }
        while (left > 0) {
            offset = next_option(cur, left, &option, &value);
            if (!offset) {
                OPCODE(ack, OPCODE_ERROR);
                *outlen = 2;
                send_message(*outgoing, *outlen, cookie);
                return TRANSFER_ERROR;
            }

            if (!strncasecmp(option, kBlkSize, strlen(kBlkSize))) { // RFC 2348
                // Valid values range between "8" and "65464" octets, inclusive
                unsigned long val = strtol(value, (char**)NULL, 10);
                if (val < 8 || val > 65464) {
                    // Invalid
                }
                if (!(session->options.requested & BLOCKSIZE_OPTION)) {
                    // This was not requested
                }

                // With an MTU of 1500, shouldn't be more than 1428
                session->block_size = val;
            } else if (!strncasecmp(option, kTimeout, strlen(kTimeout))) { // RFC 2349
                // Valid values range between "1" and "255" seconds inclusive.
                unsigned long val = strtol(value, (char**)NULL, 10);
                if (val < 1 || val > 255) {
                    // Invalid
                }
                if (!(session->options.requested & TIMEOUT_OPTION)) {
                    // This was not requested
                }
                session->timeout = val;
            } else if (!strncasecmp(option, kWindowSize, strlen(kWindowSize))) { // RFC 7440
                // The valid values range MUST be between 1 and 65535 blocks, inclusive.
                unsigned long val = strtol(value, (char**)NULL, 10);
                if (val < 1 || val > 65464) {
                    // Invalid
                }
                if (!(session->options.requested & WINDOWSIZE_OPTION)) {
                    // This was not requested
                }
                session->window_size = strtol(value, (char**)NULL, 10);
            } else {
                // Options which the server does not support should be omitted from the
                // OACK; they should not cause an ERROR packet to be generated.
            }

            cur += offset;
            left -= offset;
        }
        *timeout_ms = 1000 * session->timeout;

        fprintf(stderr, "Options negotiated\n");
        fprintf(stderr, "    Block Size : %d\n", session->block_size);
        fprintf(stderr, "    Timeout    : %d\n", session->timeout);
        fprintf(stderr, "    File Size  : %zu\n", session->file_size);
        fprintf(stderr, "    Window Size: %d\n", session->window_size);

        tftp_data_msg* ack_data = (void*)ack;
        session->offset = 0;
        session->block_number = 0;
        uint32_t ret = TRANSMIT_MORE;
        while (ret == TRANSMIT_MORE) {
            ret = _transmit(session, ack_data, outlen);
            if (*outlen > 0) {
                send_message(*outgoing, *outlen, cookie);
            }
        }
        return NO_ERROR;
    };
    case OPCODE_OERROR: {
        fprintf(stdout, "Option Error\n");
        session->state = ERROR;
        return TRANSFER_ERROR;
    }
    default:
        fprintf(stdout, "Unknown opcode\n");
        session->state = ERROR;
        return TRANSFER_ERROR;
    }
    return NO_ERROR;
}

uint32_t tftp_timeout(tftp_session* session,
                      void** outgoing,
                      size_t* outlen,
                      uint32_t* timeout_ms,
                      tftp_send_message send_message,
                      void* cookie) {
    send_message(*outgoing, *outlen, cookie);
    return 0;
}

size_t next_option(char* buffer, size_t len, char** option, char** value) {
    size_t left = len;
    size_t option_len = strnlen(buffer, left);
    if (option_len == len) {
        return 0;
    }

    *option = buffer;
    fprintf(stdout, "'%s' %ld\n", *option, option_len);
    left -= (option_len + 1);
    size_t value_len = strnlen(buffer + option_len + 1, left);
    if (value_len == left) {
        return 0;
    }
    *value = buffer + option_len + 1;
    left -= (value_len + 1);
    fprintf(stdout, "'%s' %ld\n", *value, option_len);
    return len - left;
}
