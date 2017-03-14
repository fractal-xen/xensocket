/* sender.c
 *
 * Sender side test software for testing XenSocket.
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
  //const long long tot_bytes_to_tx = 0x2000000;
  const long long tot_bytes_to_tx = 0x2000;
  int             sock;
  struct          sockaddr_xe sxeaddr;
  int             rc = 0;
  long long       i = 1;
  long long       bytes_sent = 0;
  int             j = 0;
  int             maxlen = 4096;
  int             len = maxlen;
  char            myStr[len];
  int             vary_len = 0;
  long long       bytes_remaining = 0;
  int             unsent_bytes = 0;

  if (argc > 4) {
    printf("Usage: %s <service>\n", argv[0]);
    return -1;
  }

  sxeaddr.sxe_family = AF_XEN;
  strcpy(sxeaddr.service, argv[1]);

  //sxeaddr.remote_domid = atoi(argv[1]);
  //printf("domid = %d\n", sxeaddr.remote_domid);

  //sxeaddr.shared_page_gref = atoi(argv[2]);
  //printf("gref = %d\n", sxeaddr.shared_page_gref);

  if (argc == 4) {
    vary_len = atoi(argv[3]);
  }

  /* Create the socket. */
  sock = socket (AF_XEN, SOCK_STREAM, -1);
  if (sock < 0) {
    errno = ENOTRECOVERABLE;
    perror ("socket");
    exit (EXIT_FAILURE);
  }

  rc = connect (sock, (struct sockaddr *)&sxeaddr, sizeof(sxeaddr));
  if (rc < 0) {
    printf ("connect failed\n");
    exit(1);
  }

  printf("Sending...\n");
  fflush(stdout);

  while (bytes_sent < tot_bytes_to_tx) {
    if ((vary_len) && (len != bytes_remaining)) {
      len = (i % 100) + 1;
    }

    for (j=0; j < len; j++) {
      myStr[j] = 48 + (i % 10);
    }

    /* inject an error: 
    if (i == 2) {
      myStr[4090] = 48+ (i % 10) +1;
    }
    end inject an error */

    unsent_bytes = len;
    while (unsent_bytes) {
      rc = send(sock, myStr, len, 0);
      if (rc < 0) {
        printf("send returned error = %i\n", rc);
        errno = ENOTRECOVERABLE;
        perror ("send");
        rc = 1;
        break;
      }
      bytes_sent += rc;
      unsent_bytes -= rc;

      printf("bytes_remaining= %llu, bytes_sent = %llu, tot_bytes_to_tx= %llu, len= %i\n", bytes_remaining, bytes_sent, tot_bytes_to_tx, len);

      bytes_remaining = tot_bytes_to_tx - bytes_sent;
      if ((bytes_remaining > 0) && (bytes_remaining < len)) {
        len = bytes_remaining;
        printf("new len= %i\n", len);
      }

    }
    i++;

  }

  return rc;
}

