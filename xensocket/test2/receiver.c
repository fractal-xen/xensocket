#include <stddef.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "../xensocket.h"

int main(int argc, char **argv) {
    int sock;
    int rc;
    struct sockaddr_xe sxeaddr;

    if (argc != 2) {
        printf("Usage: %s <service>\n", argv[0]);
        return -1;
    }

    sxeaddr.sxe_family = AF_XEN;
    strcpy(sxeaddr.service, argv[1]);

    printf("creating socket\n");
    sock = socket(AF_XEN, SOCK_STREAM, -1);
    if (sock < 0) {
        errno = -ENOTRECOVERABLE;
        perror("socket\n");
        exit(EXIT_FAILURE);
    }

    // receiver as server
    struct sockaddr_xe remote_sxeaddr;
    printf("binding socket\n");
    bind(sock, (struct sockaddr*) &sxeaddr, sizeof(sxeaddr));
    printf("start listening\n");
    listen(sock, 5);
    unsigned int addrlen = sizeof(remote_sxeaddr);
    int newsock = accept(sock, (struct sockaddr*)&remote_sxeaddr, &addrlen);
    printf("accepted connection\n");
    close(sock);

/*
    // receiver as client
    rc = connect (sock, (struct sockaddr*) &sxeaddr, sizeof(sxeaddr)    );
    if (rc < 0) {
        printf("connect failed\n");
        exit(1);
    }
    */

    printf("connected\n");

    while(1) {
        char buffer[4096];
        rc = recv(newsock, buffer, 4096, MSG_DONTWAIT);
        if (rc <= 0) {
            shutdown(newsock, SHUT_RDWR);
            exit(0);
        }
        else {
            buffer[rc] = '\0';
            printf("%s\n", buffer);
        }
    }
}
