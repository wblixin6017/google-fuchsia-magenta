#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "tftp/tftp.h"

static char scratch[1024];
static char out_scratch[1024];
static char in_scratch[1024];
static char* receiving;
static size_t receiving_length;

typedef struct channel channel_t;

struct channel {
    int socket;
    struct sockaddr_in out_addr;
    struct sockaddr_in in_addr;
    uint32_t previous_timeout_ms;

    int (*send)(channel_t* channel, void* data, size_t len);
    int (*receive)(channel_t* channel, void* data, size_t len);
    int (*set_timeout)(channel_t* channel, uint32_t timeout_ms);
};

int channel_send(channel_t* channel, void* data, size_t len) {
    return sendto(channel->socket, data, len, 0, (struct sockaddr*)&channel->out_addr, sizeof(struct sockaddr_in));
}

int channel_receive(channel_t* channel, void* data, size_t len) {
    socklen_t server_len;
    return recvfrom(channel->socket, data, len, 0, (struct sockaddr*)&channel->in_addr, &server_len);
}

int channel_set_timeout(channel_t* channel, uint32_t timeout_ms) {
    if (channel->previous_timeout_ms != timeout_ms && timeout_ms > 0) {
        fprintf(stdout, "Setting timeout to %dms\n", timeout_ms);
        channel->previous_timeout_ms = timeout_ms;
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = 1000 * (timeout_ms - 1000 * tv.tv_sec);
        return setsockopt(channel->socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }
    return 0;
}

channel_t* create_channel(const char* hostname, int incoming_port, int outgoing_port) {
    channel_t* channel = (channel_t*)malloc(sizeof(channel_t));
    memset(channel, 0, sizeof(channel_t));

    struct hostent* server;

    if ((channel->socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        fprintf(stderr, "Cannot create socket\n");
        free(channel);
        return NULL;
    }

    if (!(server = gethostbyname(hostname))) {
        fprintf(stderr, "Could not resolve host '%s'\n", hostname);
        free(channel);
        return NULL;
    }

    memset(&channel->out_addr, 0, sizeof(struct sockaddr_in));
    channel->out_addr.sin_family = AF_INET;
    channel->out_addr.sin_port = htons(outgoing_port);
    memcpy(&channel->out_addr.sin_addr.s_addr, server->h_addr, server->h_length);

    memset(&channel->in_addr, 0, sizeof(struct sockaddr_in));
    channel->in_addr.sin_family = AF_INET;
    channel->in_addr.sin_port = htons(incoming_port);
    memcpy(&channel->in_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    if (bind(channel->socket, (struct sockaddr*)&channel->in_addr, sizeof(struct sockaddr_in)) == -1) {
        fprintf(stderr, "Could not bind\n");
        free(channel);
        return NULL;
    }

    channel->previous_timeout_ms = 0;
    channel->send = channel_send;
    channel->receive = channel_receive;
    channel->set_timeout = channel_set_timeout;
    return channel;
}

void print_hex(uint8_t* buf, size_t len);

void print_usage() {
    fprintf(stdout, "tftp (-s filename|-r filename)\n");
    fprintf(stdout, "\t -s filename to send the provided file\n");
    fprintf(stdout, "\t -r filename to receive a file\n");
}

uint32_t send_message(void* data, size_t length, void* cookie) {
    channel_t* channel = (channel_t*)cookie;
    int n = channel->send(channel, data, length);
    print_hex(data, length);
    fprintf(stdout, "Sent %d\n", n);
    return n;
}

uint32_t receive_open_file(const char* filename,
                           size_t size,
                           void** data,
                           void* cookie) {
    fprintf(stdout, "Allocating %ld\n", size);
    receiving = (char*)malloc(size);
    receiving_length = size;
    memset(receiving, 0, size);
    *data = (void*)receiving;
    return 0;
}

int tftp_send_file(tftp_session* session,
                   const char* hostname,
                   int incoming_port,
                   int outgoing_port,
                   const char* filename) {
    channel_t* channel = create_channel(hostname, incoming_port, outgoing_port);
    if (!channel) {
        return -1;
    }

    // Open file and retrieve file size
    FILE* file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Failed to open %s\n", filename);
        return -1;
    }
    if (fseek(file, 0, SEEK_END)) {
        fprintf(stderr, "Failed to determine file size\n");
        fclose(file);
        return -1;
    }
    long file_size = ftell(file);
    if (fseek(file, 0, SEEK_SET)) {
        fprintf(stderr, "Failed to determine file size\n");
        fclose(file);
        return -1;
    }

    fprintf(stdout, "Sending %s of size %ld\n", filename, file_size);

    // FIXME we obviously don't want to load everything in memory at once!
    void* data = malloc(file_size);
    memset(data, 0, file_size);
    size_t offset = 0;
    fprintf(stdout, "Loading file into memory...");
    while (!feof(file)) {
        offset += fread(data + offset, 1, 4096, file);
    }
    fprintf(stdout, " done %zu\n", offset);

    size_t out = 1024;
    size_t in = 1024;
    void* outgoing = (void*)out_scratch;
    void* incoming = (void*)in_scratch;
    uint32_t timeout_ms = 60000;

    if (tftp_generate_write_request(session,
                                    "magenta.bin",
                                    MODE_OCTET,
                                    &data,
                                    file_size,
                                    100, // block_size
                                    0,   // timeout
                                    10,  // window_size
                                    &outgoing,
                                    &out,
                                    &timeout_ms,
                                    send_message,
                                    channel)) {
        fprintf(stderr, "Failed to generate write request\n");
        return -1;
    }

    int n, ret;
    do {
        channel->set_timeout(channel, timeout_ms);

        in = 1024;
        n = channel->receive(channel, incoming, in);
        if (n < 0) {
            if (errno == EAGAIN) {
                fprintf(stdout, "Timed out\n");
                ret = tftp_timeout(session,
                                   &outgoing,
                                   &out,
                                   &timeout_ms,
                                   send_message,
                                   channel);
                if (ret < 0) {
                    fprintf(stderr, "Failed to parse request (%d)\n", ret);
                    return -1;
                } else if (ret > 0) {
                    fprintf(stderr, "Completed\n");
                    return 0;
                }
                continue;
            } else {
                fprintf(stdout, "Failed %d\n", errno);
                return -1;
            }
        }
        fprintf(stdout, "Received %d\n", n);
        in = n;

        out = 1024;
        ret = tftp_receive(session,
                           incoming,
                           in,
                           &outgoing,
                           &out,
                           &timeout_ms,
                           NULL,
                           send_message,
                           channel);
        if (ret < 0) {
            fprintf(stderr, "Failed to parse request (%d)\n", ret);
            return -1;
        } else if (ret > 0) {
            fprintf(stderr, "Completed\n");
            return 0;
        }
    } while (1);

    return 0;
}

int tftp_receive_file(tftp_session* session,
                      const char* hostname,
                      int incoming_port,
                      int outgoing_port,
                      const char* filename) {
    channel_t* channel = create_channel(hostname, incoming_port, outgoing_port);
    size_t in = 1024;
    void* incoming = (void*)in_scratch;
    size_t out = 1024;
    void* outgoing = (void*)out_scratch;
    uint32_t timeout_ms = 60000;

    if (!channel) {
        return -1;
    }

    FILE* file = fopen(filename, "w");
    if (!file) {
        fprintf(stderr, "Failed to open %s\n", filename);
        return -1;
    }

    fprintf(stdout, "Waiting for traffic.\n");

    int n, ret;
    do {
        in = 1024;
        n = channel->receive(channel, incoming, in);
        if (n < 0) {
            if (errno == EAGAIN) {
                fprintf(stdout, "Timed out\n");
            } else {
                fprintf(stdout, "Failed to receive: -%d\n", errno);
                return -1;
            }
        } else {
            fprintf(stdout, "Received: %d\n", n);
            in = n;
        }

        out = 1024;
        ret = tftp_receive(session,
                           incoming,
                           in,
                           &outgoing,
                           &out,
                           &timeout_ms,
                           receive_open_file,
                           send_message,
                           channel);
        if (ret < 0) {
            fprintf(stderr, "Failed to parse request (%d)\n", ret);
            return -1;
        } else if (ret > 0) {
            fprintf(stderr, "Completed %zu ... ", receiving_length);
            FILE* file = fopen(filename, "w");
            out = 0;
            while (out < receiving_length) {
                size_t length = receiving_length - out < 4096 ? receiving_length - out : 4096;
                ret = fwrite(receiving + out, length, 1, file);
                if (ret <= 0) {
                    fprintf(stderr, "\nFailed to write to disk %d\n", ret);
                    fclose(file);
                    return -1;
                }
                out += length;
            }
            fclose(file);
            fprintf(stderr, "Flushed to disk\n");
            return 0;
        }
        channel->set_timeout(channel, timeout_ms);
    } while (1);
    return 0;
}

int main(int argc, char* argv[]) {
    const char* hostname = "127.0.0.1";
    int port = 2343;

    if (argc < 3) {
        print_usage();
        return 1;
    }

    tftp_session* session = NULL;
    if (tftp_init(&session, scratch, 1024)) {
        fprintf(stderr, "Failed to initialize TFTP Session\n");
        return -1;
    }

    if (!strncasecmp(argv[1], "-s", 2)) {
        return tftp_send_file(session, hostname, port, port + 1, argv[2]);
    } else if (!strncasecmp(argv[1], "-r", 2)) {
        return tftp_receive_file(session, hostname, port + 1, port, argv[2]);
    } else {
        print_usage();
        return 2;
    }
    return 0;
}
