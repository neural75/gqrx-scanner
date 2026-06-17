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
#include <stddef.h>

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
// Recv (fixed-size buffer, existing)
//
bool Recv(int sockfd, char *buf);

//
// RecvResponse — read a full response line into a dynamically allocated buffer.
// The caller must call FreeResponse(out) when done.
//
bool RecvResponse(int sockfd, char **out, size_t *out_len);

//
// FreeResponse — free a buffer allocated by RecvResponse and set *out to NULL.
//
void FreeResponse(char **out);

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

//
// GetFilterBandwidth — send "m" to Gqrx and parse the two-line response:
//   <mode_name>\n
//   <bandwidth_hz>\n
// Returns the current channel filter bandwidth in Hz.
//
bool GetFilterBandwidth(int sockfd, freq_t *bw_hz);

//
// FFT spectrum protocol (Gqrx remote-control FFT extension)
//
// fft_bw — target bin width in Hz (passed as W: argument to Gqrx)
//
bool GetFFTParameters(int sockfd, int fft_bw,
                      freq_t *center_freq, double *start_hz,
                      double *end_hz, double *bin_width,
                      int *total_bins, int *count);

bool GetFFTValues(int sockfd, int fft_bw,
                  float *values, int max_values,
                  freq_t *center_freq, double *start_hz,
                  double *end_hz, double *bin_width,
                  int *total_bins, int *count);

bool GetFFTValuesPartial(int sockfd, double start_hz, int n_bins, int fft_bw,
                         float *values, int max_values,
                         freq_t *center_freq, double *start_hz_out,
                         double *end_hz_out, double *bin_width,
                         int *total_bins, int *count_out);

#endif /* _GQRX_PROT_H_ */
