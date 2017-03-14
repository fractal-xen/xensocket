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
  int addr_len;
  int newsock;
  struct sockaddr_xe sxeaddr;
  struct sockaddr_xe remote_sxeaddr;

  if (argc != 2) {
    printf("Usage: %s <service>\n", argv[0]);
    return -1;
  }

  sxeaddr.sxe_family = AF_XEN;
  strcpy(sxeaddr.service, argv[1]);

  sock = socket(AF_XEN, SOCK_STREAM, -1);
  if (sock < 0) {
    errno = -ENOTRECOVERABLE;
    perror("socket");
    exit(EXIT_FAILURE);
  }

  printf("binding socket");
  bind(sock, (struct sockaddr*) &sxeaddr, sizeof(sxeaddr));
  printf("start listening");
  listen(sock, 5);
  addr_len = sizeof(remote_sxeaddr);
  newsock = accept(sock, (struct sockaddr*)&remote_sxeaddr, &addr_len);
  printf("accepted connection");
  close(sock);

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
