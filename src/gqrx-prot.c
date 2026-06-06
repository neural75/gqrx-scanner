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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/time.h>
#ifndef OSX
#include <linux/limits.h>
#else
#include <sys/syslimits.h>
#endif
#include <math.h>
#include "gqrx-prot.h"

//
// error - wrapper for perror
//
void error(char *msg) {
    perror(msg);
    exit(1);
}

//
// Connect
//
int Connect (char *hostname, int portno)
{
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
        fprintf(stderr,"ERROR, no such host as %s\n", hostname);
        exit(1);
    }

    /* build the server's Internet address */
    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    memcpy(&serveraddr.sin_addr.s_addr, server->h_addr_list[0],
	   server->h_length);
    serveraddr.sin_port = htons(portno);

    /* connect: create a connection with the server */
    if (connect(sockfd, (const struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0)
      error("ERROR connecting");

    /* Set receive and send timeouts so that read()/write() do not block
     * indefinitely on a broken TCP connection (e.g. after resume from
     * suspend).  3 seconds is long enough for localhost and short enough
     * that the scanner feels responsive on failure.
     * (Skipped in test builds — mocked sockets don't support these opts.) */
#ifndef TESTING_BUILD
    struct timeval tv = { .tv_sec = 3, .tv_usec = 0 };
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
        fprintf(stderr, "Warning: could not set SO_RCVTIMEO: %s\n", strerror(errno));
    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0)
        fprintf(stderr, "Warning: could not set SO_SNDTIMEO: %s\n", strerror(errno));
#endif

    return sockfd;
}
//
// Send
//
bool Send(int sockfd, char *buf)
{
    int n;

    n = write(sockfd, buf, strlen(buf));
    if (n < 0)
    {
        fprintf(stderr, "Warning: write to socket failed: %s\n", strerror(errno));
        return false;
    }
    return true;
}

//
// Recv
//
bool Recv(int sockfd, char *buf)
{
    int n;

    n = read(sockfd, buf, BUFSIZE - 1);
    if (n < 0)
    {
        fprintf(stderr, "Warning: read from socket failed: %s\n", strerror(errno));
        buf[0] = '\0';
        return false;
    }
    buf[n]= '\0';
    return true;
}


//
// GQRX Protocol
//
bool GetCurrentFreq(int sockfd, freq_t *freq)
{
    char buf[BUFSIZE];

    if (!Send(sockfd, "f\n"))
        return false;
    if (!Recv(sockfd, buf))
        return false;

    if (strcmp(buf, "RPRT 1\n") == 0 )
        return false;

    sscanf(buf, "%llu", freq);
    return true;
}
bool SetFreq(int sockfd, freq_t freq)
{
    char buf[BUFSIZE];

    sprintf (buf, "F %llu\n", freq);
    if (!Send(sockfd, buf))
        return false;
    if (!Recv(sockfd, buf))
        return false;

    if (strcmp(buf, "RPRT 1\n") == 0 )
        return false;

    freq_t freq_current = 0;
    int error_retries = 10;
    while (1)
    {
        if (!GetCurrentFreq(sockfd, &freq_current))
        {
            if (--error_retries <= 0)
                return false;
            usleep(1000);
            continue;
        }
        if (freq_current == freq)
            return true;
        usleep(1000);
    }
}

bool GetSignalLevel(int sockfd, double *dBFS)
{
    char buf[BUFSIZE];

    if (!Send(sockfd, "l\n"))
        return false;
    if (!Recv(sockfd, buf))
        return false;

    if (strcmp(buf, "RPRT 1\n") == 0 )
        return false;

    sscanf(buf, "%lf", dBFS);
    *dBFS = round((*dBFS) * 10)/10;

    if (*dBFS == 0.0)
        return false;
    return true;
}

bool GetSquelchLevel(int sockfd, double *dBFS)
{
    char buf[BUFSIZE];

    if (!Send(sockfd, "l SQL\n"))
        return false;
    if (!Recv(sockfd, buf))
        return false;

    if (strcmp(buf, "RPRT 1\n") == 0 )
        return false;

    sscanf(buf, "%lf", dBFS);
    *dBFS = round((*dBFS) * 10)/10;

    return true;
}

bool SetSquelchLevel(int sockfd, double dBFS)
{
    char buf[BUFSIZE];

    sprintf (buf, "L SQL %f\n", dBFS);
    if (!Send(sockfd, buf))
        return false;
    if (!Recv(sockfd, buf))
        return false;

    if (strcmp(buf, "RPRT 1\n") == 0 )
        return false;

    return true;
}
//
// GetSignalLevelEx
// Get a bunch of sample with some delay and calculate the mean value
//
bool GetSignalLevelEx(int sockfd, double *dBFS, int n_samp)
{
    double temp_level;
    *dBFS = 0;
    int errors = 0;
    for (int i = 0; i < n_samp; i++)
    {
        if ( GetSignalLevel(sockfd, &temp_level) )
            *dBFS = *dBFS + temp_level;
        else
            errors++;
        usleep(1000);
    }
    if (errors >= n_samp)
        return false;
    *dBFS = *dBFS / (n_samp - errors);
    return true;
}

//
// StartRecording
// Start recording audio stream to a file
//
bool StartRecording(int sockfd)
{
    char buf[BUFSIZE];

    if (!Send(sockfd, "U RECORD 1\n"))
        return false;
    if (!Recv(sockfd, buf))
        return false;

    if (strcmp(buf, "RPRT 1\n") == 0 )
        return false;

    return true;
}

//
// StopRecording
// Stop recording audio stream
//
bool StopRecording(int sockfd)
{
    char buf[BUFSIZE];

    if (!Send(sockfd, "U RECORD 0\n"))
        return false;
    if (!Recv(sockfd, buf))
        return false;

    if (strcmp(buf, "RPRT 1\n") == 0 )
        return false;

    return true;
}
