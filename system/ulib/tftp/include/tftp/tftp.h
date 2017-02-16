#pragma once

#include <stdint.h>

/**
 * This is a library that implements TFTP (RFC 1350) with support for the
 * option extension (RFC 2347) the block size (RFC 2348) timeout interval,
 * transfer size (RFC 2349) and the window size (RFC 7440) options.
 *
 * This library does not deal with the transport of the protocol itself and
 * should be able to be plugged into an existing client or server program.
 *
 * Memory management is also explicitely handled by the calling code of the
 * library, so that it can potentially be embedded in bootloaders.
 *
 * To use this library, one should initialize a TFTP Session and generate
 * a request if the transfer needs to be triggered by the consumer of this
 * library.
 *
 * Once a transfer has been succesfully started, repetitive call to the receive
 * method should be called passing incoming data. Outgoing packets will
 * be generated and delivered through the |tftp_send_message| callback.
 *
 * In the case of the passive side of the connection, the receive method should
 * be called repeatidevely as well. Upon reception of the first packet the
 * |tftp_open_file| callback will be called to allocate the memory necessary
 * to receive the file.
 *
 * A time out value is returned when calling |tftp_generate_write_request| and
 * |tftp_receive| and should be used to notify the library that the expected
 * packet was not receive within the value returned.
 **/

#define TRANSFER_ERROR -1
#define NO_ERROR 0
#define TRANSFER_COMPLETED 1

// Opaque structure
typedef struct tftp_session_t tftp_session;

typedef enum {
    MODE_NETASCII,
    MODE_OCTET,
    MODE_MAIL,
} tftp_mode;

typedef uint32_t (*tftp_open_file)(const char* filename,
                                   size_t size,
                                   void** data,
                                   void* cookie);
typedef uint32_t (*tftp_send_message)(void* data, size_t length, void* cookie);

uint32_t tftp_init(tftp_session** session, void* buffer, size_t size);

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
                                     void* cookie);

uint32_t tftp_receive(tftp_session* session,
                      void* incoming,
                      size_t inlen,
                      void** outgoing,
                      size_t* outlen,
                      uint32_t* timeout_ms,
                      tftp_open_file open_file,
                      tftp_send_message send_message,
                      void* cookie);

uint32_t tftp_timeout(tftp_session* session,
                      void** outgoing,
                      size_t* outlen,
                      uint32_t* timeout_ms,
                      tftp_send_message send_message,
                      void* cookie);
