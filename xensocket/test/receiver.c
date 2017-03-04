/* receiver.c
 *
 * Receiver side test software for testing XenSockets.
 *
 * Authors: Xiaolan (Catherine) Zhang <cxzhang@us.ibm.com>
 *          Suzanne McIntosh <skranjac@us.ibm.com>
 *          John Griffin
 *
 * History:   
 *          Suzanne McIntosh    13-Aug-07     Initial open source version
 *
 * Copyright (c) 2007, IBM Corporation
 *
 */

#include <stddef.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "../xensocket.h"
     
int
main (int argc, char **argv) {
  //const long long tot_bytes_to_rx = 0x2000000;
  const long long tot_bytes_to_rx = 0x2000;
  int             sock;
  struct          sockaddr_xe sxeaddr;
  int             rc = 0;
  int             gref;
  long long       i = 1;
  long long       bytes_received = 0;
  const int       buflen = 4096;
  int             len = buflen;
  int             vary_len = 0;
  int             no_mismatch = 1;
  int             bytes_unreceived = 0;

  if (argc > 3 || argc < 2) {
    printf("Usage: %s <peer-domid>\n", argv[0]);
    return -1;
  }

  sxeaddr.sxe_family = AF_XEN;

  sxeaddr.remote_domid = atoi(argv[1]);
  printf("domid = %d\n", sxeaddr.remote_domid);

  /* Create the socket. */
  sock = socket (21, SOCK_STREAM, -1);
  if (sock < 0) {
    errno = -ENOTRECOVERABLE;
    perror ("socket");
    exit (EXIT_FAILURE);
  }

  gref = bind (sock, (struct sockaddr *)&sxeaddr, sizeof(sxeaddr));
  printf("gref = %d\n", gref);

  if (argc == 3) {
    vary_len = atoi(argv[2]);
  }

  while ((bytes_received < tot_bytes_to_rx) && no_mismatch) {
    char buf[buflen];
    int j = 0;

    if (vary_len) {
      len = (i % 100) + 1;
    }

   bytes_unreceived = len;
   while (bytes_unreceived) {
    rc = recv(sock, buf, len, 0);
    if (rc <= 0) {
      shutdown(sock, SHUT_RDWR);
      printf("recv returned error = %i\n", rc);
      errno = ENOTRECOVERABLE;
      perror ("recv");
      rc = 1;
      break;
    }
    
    bytes_received += rc;
    bytes_unreceived -= rc;

    for (j=0; j < rc; j++) {
      if (buf[j] != 48 + (i % 10)) {
        printf("RX ERROR - DATA MISMATCH! expected=%d actual=%d\n", (int)(48+(i % 10)), buf[j]); 
        printf("iter %llu, recd %llu, The string is: %s\n", i, bytes_received, &buf[0]);
        shutdown(sock, SHUT_RDWR);
        sleep(1);
        errno = ENOTRECOVERABLE;
        perror ("recv");
        no_mismatch = 0;        
        rc = 1;
        break;
      }
    }

    printf("iter %llu, len %d, rxd %llu bytes\n", i, len, bytes_received);
    if (len > tot_bytes_to_rx - bytes_received) {
      len = tot_bytes_to_rx - bytes_received;
    }
   }

    i++;
  }
  return rc;
}

