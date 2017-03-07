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
    printf("Usage: %s <peer-domid>\n", argv[0]);
    return -1;
  }

  sxeaddr.sxe_family = AF_XEN;
  sxeaddr.remote_domid = atoi(argv[1]);

  sock = socket(AF_XEN, SOCK_STREAM, -1);
  if (sock < 0) {
    errno = -ENOTRECOVERABLE;
    perror("socket");
    exit(EXIT_FAILURE);
  }

  gref = bind(sock, (struct sockaddr*) &sxeaddr, sizeof(sxeaddr));
  printf("gref = %d\n", gref);

  while(1) {
    char buffer[4096];
    rc = recv(sock, buffer, 4096, 0);
    if (rc <= 0)
      exit(0);
    else {
      buffer[rc] = '\0';
      printf("%s\n", buffer);
    }
  }
}
