/*
MIT License

Copyright (c) 2017 neural75

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#ifndef OSX
#include <linux/limits.h>
#else
#include <sys/syslimits.h>
#endif
#include "gqrx-prot.h"
#include <math.h>

#define MAX_BUF_SIZE 1472
//
// error - wrapper for perror
//

void error(char *msg) {
  perror(msg);
  exit(0);
}

// Initiate UDP connection
int UdpConnect(char *hostname, int portno) {
  int sockfd;
  struct sockaddr_in6 server_addr;
  struct hostent *server;

  if ((sockfd = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
    perror("socket creation failed");
    exit(EXIT_FAILURE);
  }

  server = gethostbyname2(hostname, AF_INET6);

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin6_family = AF_INET6;
  inet_pton(AF_INET6, hostname, &server_addr.sin6_addr);
  server_addr.sin6_port = htons(portno); // Port number

  if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) <
      0) {
    perror("bind failed");
    exit(EXIT_FAILURE);
  }

  return sockfd;
}

// check if there is a signal via UDP
void IsThereASignal(void *args_to_pass) {
  struct sockaddr_in6 client_addr;
  socklen_t client_len = sizeof(client_addr);
  Signal_args_t *args = (Signal_args_t *)args_to_pass;
  int sockfd = *args->sockfd;
  char buffer[MAX_BUF_SIZE];
  while (1) {
    // Receive data from the socket
    ssize_t recv_len = recvfrom(sockfd, (char *)buffer, MAX_BUF_SIZE, MSG_TRUNC,
                                (struct sockaddr *)&client_addr, &client_len);
    if (recv_len < 0) {
      perror("recvfrom failed");
      exit(EXIT_FAILURE);
    }
    // Null-terminate the received data
    buffer[MAX_BUF_SIZE - 1] = '\0';

    for (int i = 0; i < MAX_BUF_SIZE - 50; i += 100) {
      if (buffer[i] != 0) {
        args->signal = true;
        break;
      } else
        args->signal = false;
    }
  }
}

int Connect(char *hostname, int portno) {
  int sockfd, n;
  struct sockaddr_in serveraddr;
  struct hostent *server;

  /* socket: create the socket */
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0)
    error("ERROR opening socket");

  /* gethostbyname: get the server's DNS entry */
  server = gethostbyname(hostname);
  if (server == NULL) {
    fprintf(stderr, "ERROR, no such host as %s\n", hostname);
    exit(0);
  }

  /* build the server's Internet address */
  bzero((char *)&serveraddr, sizeof(serveraddr));
  serveraddr.sin_family = AF_INET;
  bcopy((char *)server->h_addr, (char *)&serveraddr.sin_addr.s_addr,
        server->h_length);
  serveraddr.sin_port = htons(portno);

  /* connect: create a connection with the server */
  if (connect(sockfd, (const struct sockaddr *)&serveraddr,
              sizeof(serveraddr)) < 0)
    error("ERROR connecting");

  return sockfd;
}
//
// Send
//
bool Send(int sockfd, char *buf) {
  int n;

  n = write(sockfd, buf, strlen(buf));
  if (n < 0)
    error("ERROR writing to socket");
  return true;
}

//
// Recv
//
bool Recv(int sockfd, char *buf) {
  int n;

  n = read(sockfd, buf, BUFSIZE);
  if (n < 0)
    error("ERROR reading from socket");
  buf[n] = '\0';
  return true;
}

//
// GQRX Protocol
//
bool GetCurrentFreq(int sockfd, freq_t *freq) {
  char buf[BUFSIZE];

  Send(sockfd, "f\n");
  Recv(sockfd, buf);

  if (strcmp(buf, "RPRT 1") == 0)
    return false;

  sscanf(buf, "%llu", freq);
  return true;
}
bool SetFreq(int sockfd, freq_t freq) {
  char buf[BUFSIZE];

  sprintf(buf, "F %llu\n", freq);
  Send(sockfd, buf);
  Recv(sockfd, buf);

  if (strcmp(buf, "RPRT 1") == 0)
    return false;

  freq_t freq_current = 0;
  do {
    GetCurrentFreq(sockfd, &freq_current);
  } while (freq_current != freq);

  return true;
}

bool GetSignalLevel(int sockfd, double *dBFS) {
  char buf[BUFSIZE];

  Send(sockfd, "l\n");
  Recv(sockfd, buf);

  if (strcmp(buf, "RPRT 1") == 0)
    return false;

  sscanf(buf, "%lf", dBFS);
  *dBFS = round((*dBFS) * 10) / 10;
  if (*dBFS == 0.0)
    return false;
  return true;
}

bool GetSquelchLevel(int sockfd, double *dBFS) {
  char buf[BUFSIZE];

  Send(sockfd, "l SQL\n");
  Recv(sockfd, buf);

  if (strcmp(buf, "RPRT 1") == 0)
    return false;

  sscanf(buf, "%lf", dBFS);
  *dBFS = round((*dBFS) * 10) / 10;

  return true;
}

bool SetSquelchLevel(int sockfd, double dBFS) {
  char buf[BUFSIZE];

  sprintf(buf, "L SQL %f\n", dBFS);
  Send(sockfd, buf);
  Recv(sockfd, buf);

  if (strcmp(buf, "RPRT 1") == 0)
    return false;

  return true;
}
//
// GetSignalLevelEx
// Get a bunch of sample with some delay and calculate the mean value
//
bool GetSignalLevelEx(int sockfd, double *dBFS, int n_samp) {
  double temp_level;
  *dBFS = 0;
  int errors = 0;
  for (int i = 0; i < n_samp; i++) {
    if (GetSignalLevel(sockfd, &temp_level)) {
      *dBFS = *dBFS + temp_level;
    } else
      errors++;
    usleep(1000);
  }
  // lets promote level torward the best value
  *dBFS = *dBFS / (n_samp - errors);
  return true;
}
