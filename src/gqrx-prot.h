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
#ifndef _GQRX_PROT_H_
#include <stdbool.h>

#define _GQRX_PROT_H_

#define BUFSIZE         1024
#define FREQ_MAX        4096
#define SAVED_FREQ_MAX  1000
#define TAG_MAX         100

typedef unsigned long long freq_t;

// Demodulator mode table
// Used to convert from GQRX's mode strings in bookmarks to protocol internal mode values
// Keep this in sync with the values returned by telnet localhost 7356 -> "M ?\n"
// Current values are: OFF RAW AM AMS LSB USB CWL CWR CWU CW FM WFM WFM_ST WFM_ST_OIRT
typedef struct {
    char *mode_id;
    char *mode_descr;
} mode_table_entry_t;

static const mode_table_entry_t mode_table[] = {
    { "OFF", "Demod Off" },
    { "RAW", "Raw I/Q" },
    { "AM",  "AM" },
    { "AMS", "AM-Sync" },
    { "LSB", "LSB" },
    { "USB", "USB" },
    { "CWL", "CW-L" },
    { "CWR", "CW-R" },
    { "CWU", "CW-U" },
    { "FM",  "Narrow FM" },
    { "WFM",  "WFM (mono)" },
    { "WFM_ST",  "WFM (stereo)" },
    { "WFM_ST_OIRT",  "WFM (oirt)" }
};


//
// Global socket descriptor — used by all protocol functions.
// Set to -1 initially; assigned by Connect() in main.
// When a send/recv timeout occurs (broken connection after suspend),
// g_socket_dead is set to true and the scan loop calls Reconnect()
// to obtain a fresh descriptor.
//
extern int   g_sockfd;
extern bool  g_socket_dead;

//
// error - wrapper for perror
//
void error(char *msg);

//
// Connect
//
int Connect (char *hostname, int portno);

//
// Send
//
bool Send(int sockfd, char *buf);

//
// Recv
//
bool Recv(int sockfd, char *buf);

//
// GQRX Protocol
//
bool GetCurrentFreq(int sockfd, freq_t *freq);
bool SetFreq(int sockfd, freq_t freq);
bool SetModulationAndBandwidth (int sockfd, char *modulation, char *bandwidth);
bool GetSignalLevel(int sockfd, double *dBFS);
bool GetSquelchLevel(int sockfd, double *dBFS);
bool SetSquelchLevel(int sockfd, double dBFS);
bool GetSignalLevelEx(int sockfd, double *dBFS, int n_samp, bool calibrate);
extern unsigned long g_settle_time_us;
bool StartRecording(int sockfd);
bool StopRecording(int sockfd);

#endif /* _GQRX_PROT_H_ */
