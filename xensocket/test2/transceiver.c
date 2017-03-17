#include <stddef.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "../xensocket.h"

void test_receive(int sock) {
    char buffer[4096];
    int rc = recv(sock, buffer, 4096, MSG_DONTWAIT);
    if (rc <= 0) {
        shutdown(sock, SHUT_RDWR);
        exit(0);
    }
    else {
        buffer[rc] = '\0';
        printf("%s\n", buffer);
    }
}

void test_send(int sock) {
    int counter = 0;
    char input[4096];
    char c;
    while((c = getchar()) != '\n') {
        input[counter] = c;
        counter++;
    }
    input[counter] = '\0';

    int sent = 0;
    int len = strlen(input);
    while(sent < len && sent >= 0) {
        sent = sent + send(sock, input + sent, len - sent, 0);
        printf("Sent %d bytes\n", sent);
    }
}

int main(int argc, char **argv) {
    int sock;
    int rc;
    struct sockaddr_xe sxeaddr;

    if (argc != 3) {
        printf("Usage: %s -s|-c <service>\n", argv[0]);
        return -1;
    }

    sxeaddr.sxe_family = AF_XEN;
    strcpy(sxeaddr.service, argv[2]);
    int is_server = 0;
    if(!strcmp(argv[1], "-s")) {
        is_server = 1;
    }

    printf("creating socket\n");
    sock = socket(AF_XEN, SOCK_STREAM, -1);
    if (sock < 0) {
        errno = -ENOTRECOVERABLE;
        perror("socket\n");
        exit(EXIT_FAILURE);
    }
    int newsock;

    if(is_server) {
        struct sockaddr_xe remote_sxeaddr;
        printf("binding socket\n");
        bind(sock, (struct sockaddr*) &sxeaddr, sizeof(sxeaddr));
        printf("start listening\n");
        listen(sock, 5);
        unsigned int addrlen = sizeof(remote_sxeaddr);
        newsock = accept(sock, (struct sockaddr*)&remote_sxeaddr, &addrlen);
        printf("accepted connection\n");
        close(sock);
    } else {
        rc = connect (sock, (struct sockaddr*) &sxeaddr, sizeof(sxeaddr)    );
        if (rc < 0) {
            printf("connect failed\n");
            exit(1);
        }
        newsock = sock;
    }
    printf("connected\n");

    if(is_server) {
        test_receive(newsock);
    }
    while(1) {
        test_send(newsock);
        test_receive(newsock);
    }
}
