// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

int sock_test(int16_t port) {
    int s;

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        printf("socket failed (%d)\n", s);
        return -1;
    }
    printf("s = %d\n", s);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("bind failed\n");
        return -1;
    }

    if (listen(s, 1) < 0) {
        printf("listen failed\n");
        return -1;
    }

    int conn = accept(s, NULL, NULL);
    if (conn < 0) {
        close(s);
        printf("accept failed\n");
        return -1;
    }
    printf("connected (conn = %d)\n", conn);

    char buf[1024];
    int nread;
    nread = read(conn, buf, sizeof(buf));
    if (nread < 0) {
        printf("read failed (%d)\n", nread);
        close(conn);
        close(s);
        return -1;
    }
    printf("read success (nread = %d)\n", nread);

    for (int i = 0; i < nread; i++)
        printf("%c", buf[i]);
    printf("\n");

    int nwrite;
    nwrite = write(conn, buf, nread);
    if (nwrite < 0) {
        printf("write failed (%d)\n", nwrite);
        close(conn);
        close(s);
        return -1;
    }
    printf("write success (nwrite = %d)\n", nwrite);

    close(conn);
    close(s);

    return 0;
}

int main(int argc, char** argv) {
    int16_t port = 7;
    if (argc > 1) {
        port = atoi(argv[1]);
        printf("port is set to %d\n", port);
    }
    int r;
    r = sock_test(port);
    return r;
}
