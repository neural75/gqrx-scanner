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

//
// RecvResponse — read a full response line from the socket into a
// dynamically allocated buffer.  Grows the buffer as needed to handle
// long responses (e.g. FFT spectrum data).  The caller must free the
// output with FreeResponse().
//
// Returns the allocated buffer via *out and its length via *out_len
// (out_len may be NULL).  On failure *out is NULL and false is returned.
//
bool RecvResponse(int sockfd, char **out, size_t *out_len)
{
    size_t capacity = 4096;
    size_t total = 0;
    char *buf = malloc(capacity);
    if (!buf)
    {
        *out = NULL;
        return false;
    }

    while (1)
    {
        ssize_t n = read(sockfd, buf + total, capacity - total - 1);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            g_socket_dead = true;
            free(buf);
            *out = NULL;
            return false;
        }
        if (n == 0)
        {
            // EOF — if we have data, accept it; otherwise fail
            if (total == 0)
            {
                free(buf);
                *out = NULL;
                return false;
            }
            break;
        }
        total += (size_t)n;
        buf[total] = '\0';

        // Stop at newline (full line received)
        if (strchr(buf, '\n'))
            break;

        // Grow buffer if nearly full
        if (total >= capacity - 1)
        {
            capacity *= 2;
            char *newbuf = realloc(buf, capacity);
            if (!newbuf)
            {
                free(buf);
                *out = NULL;
                return false;
            }
            buf = newbuf;
        }
    }

    *out = buf;
    if (out_len)
        *out_len = total;
    return true;
}

//
// FreeResponse — free a buffer allocated by RecvResponse and set *out to NULL.
//
void FreeResponse(char **out)
{
    if (out && *out)
    {
        free(*out);
        *out = NULL;
    }
}

//
// GetFilterBandwidth -- send "m" to Gqrx and read the two-line response.
// Returns the current channel filter bandwidth in Hz.
//
bool GetFilterBandwidth(int sockfd, freq_t *bw_hz)
{
    char buf[BUFSIZE];

    if (!Send(sockfd, "m\n"))
        return false;
    if (!Recv(sockfd, buf))
        return false;

    if (strcmp(buf, "RPRT 1\n") == 0)
        return false;

    // Find newline separating mode name from bandwidth value
    char *nl = strchr(buf, '\n');
    if (!nl)
        return false;
    nl++;

    long val = strtol(nl, NULL, 10);
    if (val <= 0)
        return false;

    *bw_hz = (freq_t)val;
    return true;
}

//
// GetFFTParameters — send "FFT Q W:<fft_bw>" to Gqrx and parse
// the response header.  No FFT bin values are returned.
//
// Returns:
//   center_freq        — receiver's tuned center frequency (F:)
//   start_hz           — center of the first visible bin (S:)
//   end_hz             — center of the last  visible bin (E:)
//   bin_width          — pooled bin width in Hz (B:)
//   total_bins         — total bins in the full spectrum (N:)
//   count              — bins in this response (C:)
//   native_bin_width   — native FFT bin width in Hz (R:/Z:),
//                        computed from the quad_rate and fftsize
//                        fields now returned in every response
//
bool GetFFTParameters(int sockfd, int fft_bw,
                      freq_t *center_freq, double *start_hz,
                      double *end_hz, double *bin_width,
                      int *total_bins, int *count,
                      double *native_bin_width)
{
    char cmd[BUFSIZE];
    char *resp = NULL;

    /* Single pooled query — "FFT Q W:<fft_bw>".
     * The response now carries R:<rate> and Z:<fftsize> (since the
     * protocol PR #1458), so native bin width = R / Z without an
     * extra native-resolution probe. */
    snprintf(cmd, sizeof(cmd), "FFT Q W:%d\n", fft_bw);
    if (!Send(sockfd, cmd))
        return false;

    if (!RecvResponse(sockfd, &resp, NULL))
        return false;

    int rprt;
    double quad_rate;
    unsigned int fftsize;
    /* R: and Z: sit between F: and S: in the response line:
     *   RPRT 0 F:27155000 R:5000000 Z:131072 S:... E:... B:... N:... C:... */
    int matched = sscanf(resp,
                         "RPRT %d F:%llu R:%lf Z:%u "
                         "S:%lf E:%lf B:%lf N:%d C:%d",
                         &rprt, center_freq,
                         &quad_rate, &fftsize,
                         start_hz, end_hz,
                         bin_width, total_bins, count);
    FreeResponse(&resp);

    if (matched != 9 || rprt != 0)
        return false;

    if (native_bin_width)
        *native_bin_width = quad_rate / (double)fftsize;

    return true;
}

//
// GetFFTValues — send "FFT V W:<fft_bw>" to Gqrx and return
// the full spectrum (all visible bins).  Parses the header then
// fills the values[] array with up to max_values floats.
//
// To avoid repeated allocations, the caller should pre-allocate
// values[] with enough capacity for total_bins (obtained from a
// prior GetFFTParameters call).
//
bool GetFFTValues(int sockfd, int fft_bw,
                  float *values, int max_values,
                  freq_t *center_freq, double *start_hz,
                  double *end_hz, double *bin_width,
                  int *total_bins, int *count)
{
    char cmd[BUFSIZE];
    snprintf(cmd, sizeof(cmd), "FFT V W:%d\n", fft_bw);

    if (!Send(sockfd, cmd))
        return false;

    char *resp = NULL;
    if (!RecvResponse(sockfd, &resp, NULL))
        return false;

    int rprt;
    double quad_rate;
    unsigned int fftsize;
    int matched = sscanf(resp,
                         "RPRT %d F:%llu R:%lf Z:%u "
                         "S:%lf E:%lf B:%lf N:%d C:%d",
                         &rprt, center_freq,
                         &quad_rate, &fftsize,
                         start_hz, end_hz,
                         bin_width, total_bins, count);

    if (matched != 9 || rprt != 0)
    {
        FreeResponse(&resp);
        return false;
    }

    if (*count > max_values)
        *count = max_values;

    // Locate the first float after the header (after "C:<count>")
    char *p = strstr(resp, "C:");
    if (!p)
    {
        FreeResponse(&resp);
        return false;
    }
    p = strchr(p, ' ');
    if (!p)
    {
        *count = 0;
        FreeResponse(&resp);
        return true;
    }
    p++; // skip space

    for (int i = 0; i < *count; i++)
    {
        char *end;
        values[i] = strtof(p, &end);
        if (end == p)
            break; // parse failure, stop early
        p = end;
    }

    FreeResponse(&resp);
    return true;
}

//
// GetFFTValuesPartial — request a sub-range of the FFT spectrum
// via "FFT V S:<start_hz> C:<n_bins> W:<fft_bw>".
//
// This is used to request only the reliable portion of the visible
// spectrum, avoiding the filter roll-off at the band edges.
//
bool GetFFTValuesPartial(int sockfd, double start_hz, int n_bins, int fft_bw,
                         float *values, int max_values,
                         freq_t *center_freq, double *start_hz_out,
                         double *end_hz_out, double *bin_width,
                         int *total_bins, int *count_out)
{
    char cmd[BUFSIZE];
    snprintf(cmd, sizeof(cmd), "FFT V S:%.0f C:%d W:%d\n", start_hz, n_bins, fft_bw);

    if (!Send(sockfd, cmd))
        return false;

    char *resp = NULL;
    if (!RecvResponse(sockfd, &resp, NULL))
        return false;

    int rprt;
    double quad_rate;
    unsigned int fftsize;
    int matched = sscanf(resp,
                         "RPRT %d F:%llu R:%lf Z:%u "
                         "S:%lf E:%lf B:%lf N:%d C:%d",
                         &rprt, center_freq,
                         &quad_rate, &fftsize,
                         start_hz_out, end_hz_out,
                         bin_width, total_bins, count_out);

    if (matched != 9 || rprt != 0)
    {
        FreeResponse(&resp);
        return false;
    }

    if (*count_out > max_values)
        *count_out = max_values;

    // Locate the first float after the header
    char *p = strstr(resp, "C:");
    if (!p)
    {
        FreeResponse(&resp);
        return false;
    }
    p = strchr(p, ' ');
    if (!p)
    {
        *count_out = 0;
        FreeResponse(&resp);
        return true;
    }
    p++;

    for (int i = 0; i < *count_out; i++)
    {
        char *end;
        values[i] = strtof(p, &end);
        if (end == p)
            break;
        p = end;
    }

    FreeResponse(&resp);
    return true;
}

//
// CheckFFTSupport — send "FFT Q W:10000" to Gqrx and parse the response.
// Returns true if Gqrx supports the FFT remote extension (newer builds),
// false if the command fails (old Gqrx without the FFT extension).
//
bool CheckFFTSupport(void)
{
    freq_t center;
    double start, end, bw;
    int n, c;
    return GetFFTParameters(g_sockfd, 10000,
                            &center, &start, &end, &bw, &n, &c, NULL);
}

//
// GetSafeRange — compute the intersection of the visible FFT spectrum
// with the Gqrx NCO-only tuning safe zone.  The result is written to
// *p_min and *p_max.  On failure (FFT Q unsupported) both are set to 0.
//
// The NCO safe zone half-span is GQRX_NCO_SAFE_RATIO × visible_span,
// minus the demodulator filter passband/2 to guarantee that every
// SetFreq lands within the NCO-only window (avoids HW LO movement).
//
void GetSafeRange(freq_t *p_min, freq_t *p_max, freq_t *p_center)
{
    freq_t center;
    double start, end, bw;
    int total, count;
    if (!GetFFTParameters(g_sockfd, 10000, &center, &start, &end,
                          &bw, &total, &count, NULL))
    {
        *p_min = 0;
        *p_max = 0;
        *p_center = 0;
        return;
    }

    *p_center = center;

    freq_t vis_span = (freq_t)(end - start);
    freq_t safe_half = (freq_t)(GQRX_NCO_SAFE_RATIO * (double)vis_span);

    freq_t filter_bw = 0;
    if (GetFilterBandwidth(g_sockfd, &filter_bw) && filter_bw > 0)
    {
        freq_t passband_half = filter_bw / 2;
        if (safe_half > passband_half)
            safe_half -= passband_half;
        else
            safe_half = 0;
    }

    *p_min = (center > safe_half) ? center - safe_half : 0;
    *p_max = center + safe_half;
}
