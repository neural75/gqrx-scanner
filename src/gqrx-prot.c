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
    ssize_t n;

    if (g_socket_dead)
        return false;

    n = write(sockfd, buf, strnlen(buf, BUFSIZE));
    if (n < 0)
    {
        fprintf(stderr, "Warning: write to socket failed: %s\n", strerror(errno));
        g_socket_dead = true;
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
        g_socket_dead = true;
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
        if (--error_retries <= 0)
            return false;
        usleep(1000);
    }
}

//
// SetModulationAndBandwidth
// Sends "M <modulation> <bandwidth>" to Gqrx to set the demodulator mode
// and filter bandwidth on the current VFO.
// Returns false if the send/recv fails or Gqrx reports an error (RPRT != 0).
// The caller is responsible for ensuring modulation and bandwidth strings
// are valid per the Gqrx remote protocol (e.g. "FM", "9000").
//
bool SetModulationAndBandwidth (int sockfd, char *modulation, char *bandwidth)
{
    char buf[BUFSIZE];
    size_t count = sizeof(mode_table) / sizeof(mode_table_entry_t);
    int i;

    for (i = 0; i < count; i++)
    {
        if( strcmp(mode_table[i].mode_descr, modulation) == 0)
            break;
    }
    if (i >= count)
    {
        fprintf(stderr, "Warning: invalid modulation '%s'\n", modulation);
        return false;
    }

    snprintf(buf, BUFSIZE, "M %s %s\n", mode_table[i].mode_id, bandwidth);
    if (!Send(sockfd, buf))
        return false;
    if (!Recv(sockfd, buf))
        return false;

    if (strcmp(buf, "RPRT 1\n") == 0)
        return false;

    return true;
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
// Read samples from Gqrx and return the maximum level found.
//
// When calibrate == false (fast path):
//   Take n_samp quick samples 1 ms apart and return the maximum.
//   Used by the main scan loop and Debounce where speed matters.
//
// When calibrate == true (slow path):
//   Poll GetSignalLevel every 50 ms until the level stabilizes
//   (<1 dB change for 3 consecutive polls).  Track the maximum
//   level and the time it took to reach it, then update
//   g_settle_time_us via EWMA so subsequent probes use the
//   correct settle time.  Used by BacktrackFrequency and
//   AdjustFrequency after SetFreq.
//
bool GetSignalLevelEx(int sockfd, double *dBFS, int n_samp, bool calibrate)
{
    if (calibrate)
    {
        //
        // Slow calibrating path
        //
        *dBFS = -INFINITY;
        double max_level = -INFINITY;
        double settled = -INFINITY;
        double curr, initial;
        int max_time_ms = 0;
        int max_polls = (int)(g_settle_time_us / 50000);
        if (max_polls < 2)
            max_polls = 2;
        if (max_polls > 20)
            max_polls = 20;

        if (!GetSignalLevel(sockfd, &initial))
            return false;
        max_level = settled = initial;

        //
        // Poll up to max_polls times (50 ms apart).  Track when the
        // level reaches its maximum (noise -> carrier peak) and then
        // whether it stays within 1 dB for 3 consecutive polls (full
        // pipeline settle).  This gives the true settle time instead
        // of the premature time-to-max that would under-estimate.
        //
        bool max_reached = false;
        double prev_since_max = 0;
        int stable_after_max = 0;
        unsigned long settle_time_ms = 0;
        int poll_count = 0;

        for (int i = 0; i < max_polls; i++)
        {
            usleep(50000);
            if (!GetSignalLevel(sockfd, &curr))
                continue;
            settled = curr;
            poll_count = i + 1;

            if (curr > max_level)
            {
                max_level = curr;
                max_time_ms = poll_count * 50;
                max_reached = true;
                stable_after_max = 0;
                prev_since_max = curr;
            }

            //
            // After the level has started rising (max_reached), check
            // whether it stays within 1 dB for 3 consecutive polls —
            // that is the true settle time.
            //
            if (max_reached)
            {
                if (fabs(curr - prev_since_max) < 1.0)
                    stable_after_max++;
                else
                    stable_after_max = 0;
                prev_since_max = curr;

                if (stable_after_max >= 3)
                {
                    settle_time_ms = poll_count * 50;
                    break;
                }
            }
        }

        //
        // Only calibrate on a real noise -> carrier transition
        // (level rose more than 3 dB from the initial reading).
        // This rejects carrier -> noise and noise -> noise steps.
        //
        if (max_level - initial > 3.0)
        {
            unsigned long measured_us;
            if (settle_time_ms > 0)
                measured_us = settle_time_ms * 1000;   // actual settle
            else
                measured_us = poll_count * 50000;       // full duration fallback
            unsigned long old_settle = g_settle_time_us;
            g_settle_time_us = (unsigned long)(0.3 * measured_us + 0.7 * g_settle_time_us);
            fprintf(stderr, "[DIAG] settle_time: measured=%lu us, old=%lu us, new=%lu us\n",
                    measured_us, old_settle, g_settle_time_us);
        }

        *dBFS = settled;
        return true;
    }
    else
    {
        //
        // Fast path: maximum of n_samp samples, 1 ms apart
        //
        double temp_level;
        *dBFS = -INFINITY;
        int errors = 0;
        for (int i = 0; i < n_samp; i++)
        {
            if ( GetSignalLevel(sockfd, &temp_level) )
            {
                if (temp_level > *dBFS)
                    *dBFS = temp_level;
            }
            else
            {
                errors++;
            }
            usleep(1000);
        }
        return errors < n_samp;
    }
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
