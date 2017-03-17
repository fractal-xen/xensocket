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
    struct sockaddr_xe sxeaddr;
    struct sockaddr_xe remote_sxeaddr;

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

    printf("binding socket\n");
    bind(sock, (struct sockaddr*) &sxeaddr, sizeof(sxeaddr));
    printf("start listening\n");
    listen(sock, 5);
    unsigned int addr_len = sizeof(remote_sxeaddr);
    int newsock = accept(sock, (struct sockaddr*)&remote_sxeaddr, &addr_len);
    printf("accepted connection\n");
    close(sock);
    //int rc = connect (sock, (struct sockaddr*) &sxeaddr, sizeof(sxeaddr)    );

    int rc = 0;
    
    while(1) {
        char c;
	int counter = 0;
	int done = 0;

	while(!done) {
		rc = read(newsock, &c, 1);

		if (rc <= 0) {
			shutdown(newsock, SHUT_RDWR);
			exit(0);
		}

		if(c == '\n') {
			printf("Received %d bytes", counter);
			done = 1;
		}
		else {
			counter ++;
		}
	}
    }
}
