/*
MIT License

Copyright (c) 2026 neural75

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


/*
 * gqrx-scanner
 * A simple frequency scanner for gqrx
 *
 * usage: see print_usage()
 *       
 */
#define _GNU_SOURCE // strcasestr
#include <stdio.h>
#ifndef OSX
#include <stdio_ext.h>
#else
#endif
#include <stdlib.h>
#include <string.h>
#include <math.h>
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
#ifndef OSX
#include <linux/limits.h>
#else
#include <sys/syslimits.h>
#endif
#include <termios.h>
#include <time.h>
#include <getopt.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include "gqrx-prot.h"
#include "gqrx-scan.h"
#ifndef OSX
#include "vox-audio.h"
#endif

#define NB_ENABLE    true
#define NB_DISABLE   false

//
// Globals definitions
//


// Stores
/*FREQ Frequencies[FREQ_MAX] = {0};*/
FREQ* Frequencies; //if user would exceed 4096 different frequencies counter it would be bad
                   //I've took liberty of changing it to dynamic array
                   //implementation in main after ParseInputOptions()
int  Frequencies_Max = 0;

FREQ SavedFrequencies[SAVED_FREQ_MAX] = {0};
int  SavedFreq_Max = 0;

FREQ BannedFrequencies[SAVED_FREQ_MAX] = {0};
int  BannedFreq_Max = 0;

unsigned long g_settle_time_us = 500000; // default 500ms, auto-calibrated

/* Ring of buffers for print_freq so multiple calls in one printf
 * don't clobber each other (e.g. "refine %s -> %s"). */
#define PRINT_FREQ_BUFS 4
static char freq_bufs[PRINT_FREQ_BUFS][BUFSIZE];
static int freq_buf_idx = 0;

/* Gqrx's FFT pipeline advances its read pointer based on time elapsed
 * since the last call.  Rapid successive F/L reads return nearly the same
 * data.  Matching this interval (~100ms, 10 fps) ensures the next sweep
 * gets a fresh spectrum after leaving a carrier (skip/hold timeout) or
 * when nothing was found. */
#define GQRX_FFT_UPDATE_US  100000


//
// Defaults
//
const char     *g_hostname          = "localhost";
const int       g_portno            = 7356;
const freq_t    g_freq_delta        = 1000000; // +- 1Mhz default bandwidth to scan from tuned freq.
const freq_t    g_default_scan_bw   = 10000;   // default scan frequency steps (10Khz)
const freq_t    g_ban_tollerance    = 10000;   // +- 10Khz bandwidth to ban from current freq.
const long      g_delay             = 2500000; // 2.5 sec in microseconds
const char     *g_bookmarksfile     = "~/.config/gqrx/bookmarks.csv";
// Input options
//
char           *opt_hostname = NULL;
int             opt_port = 0;
freq_t          opt_freq = 0;
freq_t          opt_min_freq = 0;
freq_t          opt_max_freq = 0;
bool            has_range;
freq_t          opt_scan_bw = g_default_scan_bw;
long            opt_delay = 0; //LWVMOBILE: Changing this variable from 0 to 250 attempt to fix 'no delay argument given' stoppage on bookmark scan
//LWVMOBILE: New variables inserted here
long            opt_speed = 350000;
long            opt_date = 0;
//LWVMOBILE; End new variables.
SCAN_MODE       opt_scan_mode = sweep;
bool            opt_tag_search = false;
char           *opt_tags[TAG_MAX] = {0};
int             opt_tag_max = 0;
long            opt_max_listen = 0;
bool            opt_record = false;
// only for debug
bool            opt_verbose = false;

#ifndef OSX
bool            opt_vox = false;
long            opt_max_probe = 0;
#endif

bool            g_gqrx_supports_fft = false;

int             g_sockfd = -1;
bool            g_socket_dead = false;

#ifdef TESTING_BUILD
int             g_testing_max_full_sweeps = -1;
int             g_testing_sweep_full_count = 0;
int             g_testing_max_bookmark_loops = -1;
int             g_testing_bookmark_loop_count = 0;
#endif



//
// ParseInputOptions
//
void print_usage ( char *name )
{

    printf ("Usage:\n");
    printf ("%s\n\t\t[-h|--host <host>] [-p|--port <port>] [-m|--mode <sweep|bookmark|fft>]\n", name);
    printf ("\t\t[-f <central frequency>] [-b|--min <from freq>] [-e|--max <to freq>]\n");
    printf ("\t\t[-d|--delay <lingering time in milliseconds>]\n");
    printf ("\t\t[-l|--max-listen <[probe_time:]hangup_time>]\n");
    printf ("\t\t[-t|--tags <\"tag1|tag2|...\">]\n");
    printf ("\t\t[-v|--verbose]\n");
    printf ("\t\t[-r|--record]\n");
    printf ("\n");
    printf ("-h, --host <host>            Name of the host to connect. Default: localhost\n");
    printf ("-p, --port <port>            The number of the port to connect. Default: 7356\n");
    printf ("-m, --mode <mode>            Scan mode to be used. Default: sweep\n");
    printf ("                               Possible values for <mode>: sweep, bookmark, fft\n");
    printf ("                               fft uses the Gqrx FFT extension for faster scanning\n");
    printf ("-f, --freq <freq>            Frequency to scan with a range of +- 1MHz.\n");
    printf ("                               Default: the current frequency tuned in Gqrx Incompatible with -b, -e\n");
    printf ("-b, --min <freq>             Frequency range begins with this <freq> in Hz. Incompatible with -f\n");
    printf ("-e, --max <freq>             Frequency range ends with this <freq> in Hz. Incompatible with -f\n");
    printf ("-s, --step <freq>            Frequency step <freq> in Hz. Default: %llu\n", g_default_scan_bw);
    printf ("-d, --delay <time>           Lingering time in milliseconds before the scanner reactivates. Default 2500\n");
    printf ("-l, --max-listen <time>\n");
    printf ("                               Maximum time to listen to an active frequency. Default 0 (no limit).\n");
    printf ("                               With --vox, use -l [probe_time:]hangup_time\n");
    printf ("                               probe_time (ms): Time to wait for voice on a new carrier.\n");
    printf ("                               hangup_time (ms): Time to wait after voice drops.\n");
    printf ("                               Example: --vox -l 1000:5000\n");
    printf ("-x, --speed <time>           Time in milliseconds for bookmark scan settle delay.\n");
    printf ("                               Default: 350 milliseconds.\n");
    printf ("                               If scan lands on wrong bookmark during search, increase this value.\n");
    printf ("-y  --date                   Date Format, default is 0.\n");
    printf ("                               0 = mm-dd-yy\n");
    printf ("                               1 = dd-mm-yy\n");
    printf ("-t, --tags <\"tags\">          Filter signals. Match only on frequencies marked with a tag found in \"tags\"\n");
    printf ("                               \"tags\" is a quoted string with a '|' list separator: Ex: \"Tag1|Tag2\"\n");
    printf ("                               tags are case insensitive and match also for partial string contained in a tag\n");
    printf ("                               Works only with -m bookmark scan mode\n");
    printf ("-r, --record                  Enable recording of detected signals\n");
#ifndef OSX
    printf ("--vox                         Enable voice-activity detection (Linux only).\n");
    printf ("                               Requires: gqrx-scan-setup-audio.sh attach\n");
    printf ("                               Uses -l [probe_time:]hangup_time — see -l help.\n");
#endif
    printf ("-v, --verbose                Output more information during scan (used for debug). Default: false\n");
    printf ("--help                       This help message.\n");
    printf ("\n");
    printf ("Examples:\n");
    printf ("%s -m bookmark --min 430000000 --max 431000000 --tags \"DMR|Radio Links\"\n", name);
    printf ("\tPerforms a scan using Gqrx bookmarks, monitoring only the frequencies\n");
    printf ("\ttagged with \"DMR\" or \"Radio Links\" in the range 430MHz-431MHz\n");
    printf ("%s --min 430000000 --max 431000000 -d 3000\n", name);
    printf ("\tPerforms a sweep scan from frequency 430MHz to 431MHz, using a delay of \n");
    printf ("\t3 secs as idle time after a signal is lost, restarting the sweep loop when this time expires\n");
    printf ("\n");
    printf ("Full documentation available at <https://github.com/neural75/gqrx-scanner>\n");

    exit (EXIT_FAILURE);
}


bool ParseTags (char *tags)
{
    char *tag = NULL;

    tag = strtok (tags, "|");

    int k = 0;
    while (tag != NULL && k < TAG_MAX)
    {
        int len =  strlen(tag) + 1 ;
        opt_tags[k] = calloc(sizeof(char), len);
        strncpy(opt_tags[k], tag, len);

        tag = strtok(NULL, "|");
        k++;
    }
    opt_tag_max = k;
    if (k == 0) // wtf
    {
        printf ("Error: -t option requires a '|' separator for list of tags.\n");
        return false;
    }
    return true;
}

bool ParseInputOptions (int argc, char **argv)
{
  int c;

  while (1)
    {
      static struct option long_options[] =
        {
          /* These options set a flag. */
          //{"verbose", no_argument,       &opt_verbose, 1},
          /* These options don’t set a flag.
             We distinguish them by their indices. */
          {"verbose", no_argument,       0, 'v'},
          {"help",    no_argument,       0, 'w'},
          {"host",    required_argument, 0, 'h'},
          {"port",    required_argument, 0, 'p'},
          {"mode",    required_argument, 0, 'm'},
          {"freq",    required_argument, 0, 'f'},
          {"min",     required_argument, 0, 'b'},
          {"max",     required_argument, 0, 'e'},
          {"step",    required_argument, 0, 's'},
          {"tags",    required_argument, 0, 't'},
          {"delay",   required_argument, 0, 'd'},
          {"speed",   required_argument, 0, 'x'},
          {"date",    required_argument, 0, 'y'},
          {"max-listen",       required_argument, 0, 'l'},
          {"record", no_argument, 0, 'r'},
#ifndef OSX
          {"vox",    no_argument, 0, 'V'},
#endif
          {0, 0, 0, 0}
        };
        /* getopt_long stores the option index here. */
        int option_index = 0;

        c = getopt_long (argc, argv, "vVwh:p:m:f:b:e:s:t:d:x:y:l:r",
                        long_options, &option_index);

        // warning: I don't know why but required argument are not so "required"
        //          if a following option is encountered getopt_long returns this option as the argument in optarg
        //          instead of error, but if there is only one option with a missing arg then it returns an error.
        //

        /* Detect the end of the options. */
        if (c == -1)
            break;

        switch (c)
        {
            case 0:
                /* If this option set a flag, do nothing else now. */
                if (long_options[option_index].flag != 0)
                    break;
                printf ("option %s", long_options[option_index].name);
                if (optarg)
                    printf (" with arg %s", optarg);
                printf ("\n");
            break;
            case 'v':
                opt_verbose = true;
            break;
            case 'h':
                if (optarg[0] == '-')
                {
                    printf ("Error: -%c: option requires an argument\n", c);
                    print_usage(argv[0]);
                }
                opt_hostname = optarg;
            break;
            case 'p':
                if (optarg[0] == '-')
                {
                    printf ("Error: -%c: option requires an argument\n", c);
                    print_usage(argv[0]);
                }

                if ((opt_port = atoi (optarg)) == 0)
                {
                    printf("Error: -%c: invalid port\n", c);
                    print_usage(argv[0]);
                }
            break;
            case 'm':
                if (optarg[0] == '-')
                {
                    printf ("Error: -%c: option requires an argument\n", c);
                    print_usage(argv[0]);
                }

                if (strcmp (optarg, "sweep") == 0)
                    opt_scan_mode = sweep;
                else if (strcmp (optarg, "bookmark") == 0)
                    opt_scan_mode = bookmark;
                else if (strcmp (optarg, "fft") == 0)
                    opt_scan_mode = fft;
                else
                {
                    printf ("Error: -m, --mode <mode>. Mode not recognized. \n");
                    print_usage(argv[0]);
                }
            break;
            case 'f':
                if (optarg[0] == '-')
                {
                    printf ("Error: -%c: option requires an argument\n", c);
                    print_usage(argv[0]);
                }

                if ((opt_freq = atoll(optarg)) == 0)
                {
                    printf ("Error: -%c: Invalid frequency\n", c);
                    print_usage(argv[0]);
                }
                if (opt_freq > g_freq_delta)
                {
                    opt_min_freq = opt_freq - g_freq_delta;
                    opt_max_freq = opt_freq + g_freq_delta;
                }
                else
                {
                    printf ("Error: -%c: Invalid frequency\n", c);
                    print_usage(argv[0]);
                }
            break;
            case 'b':
                if (optarg[0] == '-')
                {
                    printf ("Error: -%c: option requires an argument\n", c);
                    print_usage(argv[0]);
                }

                if ((opt_min_freq = atoll(optarg)) == 0)
                {
                    printf ("Error: -%c: Invalid frequency\n", c);
                    print_usage(argv[0]);
                }

            break;
            case 'e':
                if (optarg[0] == '-')
                {
                    printf ("Error: -%c: option requires an argument\n", c);
                    print_usage(argv[0]);
                }

                if ((opt_max_freq = atoll(optarg)) == 0)
                {
                    printf ("Error: -%c: Invalid frequency\n", c);
                    print_usage(argv[0]);
                }
            break;
            case 'd':
                if (optarg[0] == '-')
                {
                    printf ("Error: -%c: option requires an argument\n", c);
                    print_usage(argv[0]);
                }

                if ((opt_delay = atol(optarg)) == 0)
                {
                    printf ("Error: -%c: Invalid delay\n", c);
                    print_usage(argv[0]);
                }
                opt_delay *= 1000; // in microsec
            break;

            case 'l':
                if (optarg[0] == '-')
                {
                    printf ("Error: -%c: option requires an argument\n", c);
                    print_usage(argv[0]);
                }
                {
                    char *colon = strchr(optarg, ':');
                    if (colon)
                    {
                        // Format: "probe_time:hangup_time"
                        *colon = '\0';
                        long probe = atol(optarg);
                        long hangup = atol(colon + 1);
                        if (probe <= 0 || hangup <= 0)
                        {
                            printf ("Error: -%c: Invalid time(s)\n", c);
                            print_usage(argv[0]);
                        }
#ifndef OSX
                        opt_max_probe  = probe * 1000;   // ms → µs
#endif
                        opt_max_listen = hangup * 1000;
                    }
                    else
                    {
                        // Single value — both probe and hangup
                        if ((opt_max_listen = atol(optarg)) == 0)
                        {
                            printf ("Error: -%c: Invalid time\n", c);
                            print_usage(argv[0]);
                        }
                        opt_max_listen *= 1000;           // ms → µs
#ifndef OSX
                        opt_max_probe = opt_max_listen;   // same value for probe
#endif
                    }
                }
            break;

            case 'x':
                if (optarg[0] == '-')
                {
                    printf ("Error: -%c: option requires an argument\n", c);
                    print_usage(argv[0]);
                }

                if ((opt_speed = atol(optarg)) == 0)
                {
                    printf ("Error: -%c: Invalid speed\n", c);
                    print_usage(argv[0]);
                }
                opt_speed *= 1000; // in microsec //LWVMOBILE: Made new opt_speed variable. Implemented and working for bookmark mode.
            break;

            case 'y':
                if (optarg[0] == '-')
                {
                    printf ("Error: -%c: option requires an argument\n", c);
                    print_usage(argv[0]);
                }
                errno = 0;
                char *endptr = NULL;
                opt_date = strtol(optarg,&endptr,10);
                if (errno != 0)
                {
                    printf ("Error: -%c: Invalid date option\n", c);
                    print_usage(argv[0]);
                }
            break;

            case 't':
                if (optarg[0] == '-')
                {
                    printf ("Error: -%c: option requires an argument\n", c);
                    print_usage(argv[0]);
                }

                optind--;
                if (!ParseTags(argv[optind]))
                    print_usage(argv[0]);
                optind++;
                opt_tag_search = true;
            break;

            case 's':
                if (optarg[0] == '-')
                {
                    printf ("Error: -%c: option requires an argument\n", c);
                    print_usage(argv[0]);
                }

                if ((opt_scan_bw = atoll(optarg)) == 0)
                {
                    printf ("Error: -%c: Invalid frequency step\n", c);
                    print_usage(argv[0]);
                }
                break;
            case 'r':
                opt_record = true;
                break;
#ifndef OSX
            case 'V':
                opt_vox = true;
                break;
#endif
            case '?':
            /* getopt_long already printed an error message. */
            case ':':
            default:
                print_usage(argv[0]);
        }
    }
    return true;
}




//
// Utilities
//
// return a statically allocated string of the freq to be printed out.
// Uses a ring of buffers so multiple calls in the same printf are safe.
char * print_freq (freq_t freq)
{
    // fist round up to khz
    freq = round ( freq / 1000.0 ) * 1000.0;
    long Ghz = freq/1000000000;
    long Mhz = (freq/1000000)%1000;
    long Khz = (freq/1000)%1000;

    char *buf = freq_bufs[freq_buf_idx];
    freq_buf_idx = (freq_buf_idx + 1) % PRINT_FREQ_BUFS;
    buf[0] = '\0';
    char temp[256];
    if (Ghz)
    {
        sprintf (temp, "%ld.%3.3ld.%3.3ld GHz", Ghz, Mhz, Khz);
        strcat(buf, temp);
        return buf;
    }
    if (Mhz)
    {
        sprintf (temp, "%ld.%3.3ld MHz", Mhz, Khz);
        strcat(buf, temp);
        return buf;
    }

    sprintf (temp, "%ld KHz", Khz);
    strcat(buf, temp);
    return buf;
}


//
// Wait a key press
//
int kbhit(void)
{
    struct timeval tv;
    fd_set fds;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds); //STDIN_FILENO is 0
    select(STDIN_FILENO+1, &fds, NULL, NULL, &tv);
    return FD_ISSET(STDIN_FILENO, &fds);
}
//
// Set/Reset non blocking mode
//
void nonblock(int state)
{
    struct termios ttystate;

    //get the terminal state
    tcgetattr(STDIN_FILENO, &ttystate);

    if (state==NB_ENABLE)
    {
        //turn off canonical mode
        ttystate.c_lflag &= ~ICANON;
        //minimum of number input read.
        ttystate.c_cc[VMIN] = 1;
    }
    else if (state==NB_DISABLE)
    {
        //turn on canonical mode
        ttystate.c_lflag |= ICANON;
    }
    //set the terminal attributes.
    tcsetattr(STDIN_FILENO, TCSANOW, &ttystate);
}
//
// GetTime
// Get the time stamp dd-mm-yy hh:mm:ss
//
struct timeval GetTime(char *timestamp)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t etime = tv.tv_sec;
    struct tm *ltime = localtime (&etime);
    switch (opt_date)
    {
	    case 0:
    		sprintf(timestamp, "%2.2d-%2.2d-%2.2d %2.2d:%2.2d:%2.2d", ltime->tm_mon+1, ltime->tm_mday, ltime->tm_year%100,
            		ltime->tm_hour, ltime->tm_min, ltime->tm_sec);
		break;
	    case 1:
	    	sprintf(timestamp, "%2.2d-%2.2d-%2.2d %2.2d:%2.2d:%2.2d", ltime->tm_mday, ltime->tm_mon+1, ltime->tm_year%100,
        	        ltime->tm_hour, ltime->tm_min, ltime->tm_sec);
		break;
    }
    return tv;
}

// Calculate difference in time in [dd days][hh:][mm:][ss secs]
void DiffTime(char *timestamp, struct timeval *start)
{
    struct timeval now;
    gettimeofday(&now, NULL);
    long diff_us = (now.tv_sec - start->tv_sec) * 1000000L
                 + (now.tv_usec - start->tv_usec);
    if (diff_us < 0) diff_us = 0;
    time_t elapsed = diff_us / 1000000;
    struct tm *ltime = localtime(&elapsed);
    timestamp[0] = '\0';


    if (ltime->tm_mday > 1)
    {
          char days[32];
          snprintf(days, sizeof(days), "%2d days ", ltime->tm_mday);
          strcat(timestamp, days);
    }
    if (ltime->tm_hour > (int)(ltime->tm_gmtoff/3600))
    {
          char hours[16];
          snprintf(hours, sizeof(hours), "%2.2d:", (int)(ltime->tm_hour - (ltime->tm_gmtoff/3600)) );
          strcat(timestamp, hours);
    }

    if (ltime->tm_min > 0)
    {
        char min[16];
        sprintf(min, "%2.2d:", ltime->tm_min);
        strcat(timestamp, min);
    }
        char sec[32];
        sprintf(sec, "%2.2ld sec", (long)ltime->tm_sec);
        strcat(timestamp, sec);
}

//
// CheckUserInput
//
// Clear the bans if 'c' is pressed during the scan cycles
//
void CheckUserInput (void)
{
    int     hit = 0;
    char    c;
    bool    pause = false;
    long    sleep = 100000; // 100 ms
#ifndef OSX
    __fpurge(stdin);
#else
    fpurge(stdin);
#endif
    nonblock(NB_ENABLE);
    do
    {
        hit = kbhit();
        if (hit !=  0)
        {
            c = fgetc(stdin);
            switch (c)
            {
                case 'c':
                {
                    // Clear all bans
                    ClearAllBans();
                    continue;
                }
                case 'p':
                {
                    // pause until another 'p'
                    pause ^= true; // switch pause mode
                    break;
                }
                case 'q':
                {
                    // quit
                    exit(0);
                }
                default:
                    break;
            }
        }
        if (pause)
        {
            usleep (sleep);
            continue;
        }

    } while ( hit != 0 || pause );

    nonblock(NB_DISABLE);
    return;
}


//
// Forward declarations
//
bool Reconnect(void);

//
// listen_timer — wall-clock timer for WaitUserInputOrDelay.
// All timing is in µs since start, precise regardless of TCP/VAD delays.
//
typedef struct {
    struct timeval start;
    long           elapsed_us;     // µs since start (set by tick)
    long           silence_since;  // µs when silence began (0 = not silent)
    long           drop_since;     // µs when drop began (0 = not dropped)
    bool           voice_heard;
} listen_timer_t;

static inline void listen_timer_init(listen_timer_t *t)
{
    gettimeofday(&t->start, NULL);
    t->elapsed_us = t->silence_since = t->drop_since = 0;
    t->voice_heard = false;
}

static inline void listen_timer_tick(listen_timer_t *t)
{
    struct timeval now;
    gettimeofday(&now, NULL);
    t->elapsed_us = (now.tv_sec - t->start.tv_sec) * 1000000L
                  + (now.tv_usec - t->start.tv_usec);
}

static inline void listen_timer_on_voice(listen_timer_t *t)
{
    t->voice_heard = true;
    t->silence_since = 0;
    t->drop_since = 0;
}

static inline void listen_timer_on_silence(listen_timer_t *t)
{
    if (!t->silence_since)
        t->silence_since = t->elapsed_us;
}

static inline void listen_timer_on_drop(listen_timer_t *t)
{
    if (!t->drop_since)
        t->drop_since = t->elapsed_us;
    /* Reset the VOX silence counter — the carrier is gone, so any
     * previous "no voice" measurement is stale.  When the carrier
     * returns the VOX probe/hangup will start fresh. */
    t->silence_since = 0;
}

static inline void listen_timer_on_signal(listen_timer_t *t)
{
    t->drop_since = 0;
}

// Returns true when probe (no voice heard yet) or hangup (threshold ms of
// consecutive silence after voice) expires.  silence_since == 0 means "not
// currently in silence" — exit can never fire in that state.
static inline bool listen_timer_should_exit(listen_timer_t *t,
    long probe_us, long hangup_us)
{
    if (t->voice_heard)
        return t->silence_since && hangup_us > 0
            && t->elapsed_us - t->silence_since >= hangup_us;
    return probe_us > 0 && t->elapsed_us >= probe_us;
}

// Returns true when below-squelch duration exceeds delay.
static inline bool listen_timer_drop_expired(listen_timer_t *t, long delay_us)
{
    return t->drop_since && (t->elapsed_us - t->drop_since > delay_us);
}


//
// WaitUserInputOrDelay
// Waits for user input or a delay after the carrier is gone
// Returns if the user has pressed <space> or <enter> to skip frequency
//
bool WaitUserInputOrDelay(long delay, freq_t *current_freq)
{
    double    squelch;
    double    level;
    long      vox_sample_time = 10000;  // 10 ms VOX audio capture window (µs)
    int       exit = 0;
    char      c;
    bool      skip = false;
    bool      pause = false;
    int       socket_failures = 0;
    listen_timer_t tmr;

    listen_timer_init(&tmr);

#ifndef OSX
    __fpurge(stdin);
#else
    fpurge(stdin);
#endif
    nonblock(NB_ENABLE);

    do
    {
        // If the socket was marked dead by a protocol function, reconnect
        // before trying again.
        if (g_socket_dead)
            Reconnect();

        // If any of the TCP reads fails (e.g. timeout after resume from
        // suspend), skip the rest of this iteration and try again after
        // a short sleep rather than blocking or acting on stale data.
        if (!GetCurrentFreq(g_sockfd,  current_freq) ||
            !GetSquelchLevel(g_sockfd, &squelch)     ||
            !GetSignalLevel(g_sockfd,  &level ))
        {
            socket_failures++;
            if (socket_failures >= 3)
            {
                g_socket_dead = true;
                exit = 1;
                skip = true;
                break;
            }
            usleep(100000);
            continue;
        }
        socket_failures = 0;

        exit = kbhit();
        if (exit != 0)
        {
            c = fgetc(stdin);
            switch (c)
            {
                case ' ':
                case '\n':  { exit = 1; skip = true; break; }
                case 'b':   { BanFreq(*current_freq); exit = 1; skip = true; break; }
                case 'c':   { ClearAllBans(); exit = 0; break; }
                case 'p':   { pause ^= true; exit = 0; break; }
                default:      exit = 0;
            }
            if (exit == 1) break;
        }

        if (pause) { usleep(100000); continue; }

        // ------ VOX detection & timing ------
#ifndef OSX
        if (opt_vox && level >= squelch)
        {
            if (VoxAudioHasSignal(vox_sample_time))
                listen_timer_on_voice(&tmr);
            else
            {
                listen_timer_on_silence(&tmr);
                if (!VoxAudioIsAlive() && !VoxAudioRestart())
                {
                    fprintf(stderr, "[ WARNING ] Audio capture pipe closed. "
                            "Disabling VOX.\n");
                    opt_vox = false;
                }
            }

            // Tick after VAD so tmr.elapsed_us includes VAD processing time.
            listen_timer_tick(&tmr);

            if (listen_timer_should_exit(&tmr, opt_max_probe, opt_max_listen))
            { exit = 1; skip = true; }
        }
        else
#endif
        {
            listen_timer_tick(&tmr);
            /* Only enforce the listen-time cap when the carrier
             * is active.  During a drop, the -d drop timer governs. */
            if (opt_max_listen != 0 && level >= squelch &&
                tmr.elapsed_us >= opt_max_listen)
            { exit = 1; skip = true; }
        }

        // ------ Signal drop ------
        if (level < squelch)
        {
            listen_timer_on_drop(&tmr);
            if (listen_timer_drop_expired(&tmr, delay))
            { exit = 1; skip = false; }
        }
        else
            listen_timer_on_signal(&tmr);

        usleep(100000);
    } while (!exit);

    nonblock(NB_DISABLE);

    // restart scanning
    *current_freq += g_ban_tollerance;
    *current_freq = ceil(*current_freq / (double)opt_scan_bw) * opt_scan_bw;

#ifndef OSX
    __fpurge(stdin);
#else
    fpurge(stdin);
#endif
    return skip;
}

//
// Open
//
FILE * Open (const char * filename)
{
    FILE * filefd;
    const char *homedir;
    char filename2[PATH_MAX];

    if (filename[0] == '~')
    {
        struct passwd *pw = getpwuid(getuid());
        homedir = pw->pw_dir;
        sprintf(filename2, "%s%s", homedir, filename+1);
    }
    else
        sprintf(filename2, "%s", filename);


    filefd = fopen (filename2, "r");
    if (filefd == (FILE *)NULL)
        error("ERROR opening gqrx bookmarks file");

    return filefd;
}

bool prefix(const char *pre, const char *str)
{
    return strncmp(pre, str, strlen(pre)) == 0;
}

//
// LoadFrequencies from gqrx file format
//
bool LoadFrequencies (FILE *bookmarksfd)
{
    char buf[BUFSIZE];
    char tmp_buf[BUFSIZE];
    char *line;
    bool start = false;
    char *freq, *other;
    int i = 0;
    int s, e;

    while (1)
    {
        line = fgets(buf, BUFSIZE, bookmarksfd );
        if (line == (char *) NULL)
            break;

        if (prefix("# Frequency ;", line))
        {
            start = true;
            continue;
        }

        if (start)
        {
            char * token = strtok(line, ";"); // freq
            if ((token == NULL) || (sscanf(token, "%llu", &Frequencies[i].freq) < 0))
              continue; // skip empty lines
            if ((token = strtok(NULL, ";")) == NULL) // descr
              continue; // skip invalid lines
            strncpy(Frequencies[i].descr, token , BUFSIZE);

            if ((token = strtok(NULL, ";")) == NULL) // modulation
              continue; // mode not found
            strncpy(tmp_buf, token , BUFSIZE);
            for (s = 0; isspace(tmp_buf[s]) ; s++); // exclude initial spaces
            for (e=strlen(tmp_buf)-1; isspace(tmp_buf[e]) ; e--); // exclude trailing spaces
            tmp_buf[e+1] = '\0'; // trim trailing spaces
            strncpy(Frequencies[i].modulation, &tmp_buf[s] , BUFSIZE);

            if ((token = strtok(NULL, ";")) == NULL) // bw
              continue;
            strncpy(tmp_buf, token , BUFSIZE);
            for (s = 0; isspace(tmp_buf[s]) ; s++); // exclude initial spaces
            for (e=strlen(tmp_buf)-1; isspace(tmp_buf[e]) ; e--); // exclude trailing spaces
            tmp_buf[e+1] = '\0'; // trim trailing spaces
            strncpy(Frequencies[i].bandwidth, &tmp_buf[s] , BUFSIZE);

            token = strtok(NULL, ";"); // tags, comma separated
            if (token == NULL) continue; // skip invalid lines
            char * tag = strtok(token,",\n");
            int k = 0;
            while (tag != NULL && k < TAG_MAX)
            {
                int len =  strlen(tag) + 1 ;
                Frequencies[i].tags[k] = calloc(sizeof(char), len);
                // exclude initial spaces
                int s;
                for (s = 0; isspace(tag[s]) ; s++);
                strncpy(Frequencies[i].tags[k], &tag[s], len);

                k++;
                tag = strtok (NULL, ",\n");
            }
            Frequencies[i].tag_max = k;
            //printf(":%llu: %s\n", Frequencies[i].freq, Frequencies[i].descr); //$$$
            i++;
            if (i >= FREQ_MAX)
            {
                printf("Warning: Too many frequencies in bookmarks file, max %d\n", FREQ_MAX);
                break;
            }
        }
    }
    Frequencies_Max = i;
    return true;
}

//
// FilterFrequency
// Use specified tags (if any) to return the frequency matching the tag
// return 0 otherwise
freq_t FilterFrequency (int idx)
{
    freq_t current_freq = Frequencies[idx].freq;
    if (!opt_tag_search)
        return current_freq;

    bool found = false;
    for (int i = 0; i < Frequencies[idx].tag_max ; i++)
    {
        char *tag = Frequencies[idx].tags[i]; // tag to search
        for (int k = 0; k < opt_tag_max; k++)
        {
            if (strcasestr(tag , opt_tags[k]) != NULL) // ignore case
            {
                found = true;
                break;
            }
        }
        if (found)
            break;
    }
    if (!found)
        return (freq_t) 0;

    return current_freq;
}

bool ScanBookmarkedFrequenciesInRange(freq_t freq_min, freq_t freq_max)
{

    freq_t freq = 0;
    GetCurrentFreq(g_sockfd, &freq);
    double level = 0;
    GetSignalLevel(g_sockfd, &level );
    double squelch = 0;
    GetSquelchLevel(g_sockfd, &squelch);

    freq_t current_freq = freq_min;

    bool skip = false;
    long sleep_cycle_active = 500000;   // skipping from active frequency need more time to wait squelch level to kick in
    long sleep_cyle_saved   = 85000 ;   // skipping freqeuency need more time to get signal level

    long slow_scan_cycle    = 1000000;   // LWVMOBILE: Just doubling numbers to slow down scan time in bookmark search, 1,000,000 = 1 second. EDIT: DOES THIS VARIABLE DO ANYTHING?
    long slow_cycle_saved   = 250000;  // LWVMOBILE: Just doubling numbers to slow down scan time in bookmark search. THIS ONE SEEMS TO ACTUALLY SLOW SCAN SPEED DOWN.
    char timestamp[BUFSIZE] = {0};
    while (true)
    {
        CheckUserInput();

        for (int i = 0; i < Frequencies_Max; i++)
        {
            if ((current_freq = FilterFrequency(i)) == (freq_t) 0 )
                continue;
            if (IsBannedFreq(&current_freq))
                continue;
            if ( ( ( current_freq >= freq_min) &&         // in the valid range
                   ( current_freq <  freq_max)    ) ||
                 (freq_min == freq_max)                )  // or using the entire frequencies
                {
                    // Found a bookmark in the range
                    if (g_socket_dead)
                        Reconnect();
                    SetFreq(g_sockfd, current_freq);
#ifndef OSX
                    if (opt_vox) VoxAudioReset();
#endif
                    SetModulationAndBandwidth (g_sockfd, Frequencies[i].modulation, Frequencies[i].bandwidth);
                    GetSquelchLevel(g_sockfd, &squelch);
                    usleep((skip) ? slow_scan_cycle : opt_speed);
                    skip = false;   // settle already applied — don't carry forward to next bookmarks
                    GetSignalLevelEx(g_sockfd, &level, 5, false);
                    if (level >= squelch)
                    {
                        if (opt_record)
                        {
                            StartRecording(g_sockfd);
                        }
                        struct timeval hit_time = GetTime(timestamp);
                        printf ("[%s] Freq: %s active [%s], Level: %2.2f/%2.2f ",
                                timestamp, print_freq(current_freq),
                                Frequencies[i].descr, level, squelch);
                        fflush(stdout);
                        skip = WaitUserInputOrDelay(opt_delay, &current_freq);
                        DiffTime(timestamp, &hit_time);
                        if (opt_record)
                        {
                            StopRecording(g_sockfd);
                        }
                        printf (" [elapsed time %s]\n", timestamp);
                        fflush(stdout);
                    }
                    else
                    {
                        skip = false;
                    }
                }
        }

#ifdef TESTING_BUILD
        g_testing_bookmark_loop_count++;
        if (g_testing_max_bookmark_loops > 0 &&
            g_testing_bookmark_loop_count >= g_testing_max_bookmark_loops)
            return true;
#endif

    }

}

// Structure for sorting FFT bins by level (peak finder)
typedef struct {
    int   idx;
    float level;
} peak_t;

// Comparator for descending sort by level
static int cmp_peak_desc(const void *a, const void *b)
{
    float va = ((const peak_t *)a)->level;
    float vb = ((const peak_t *)b)->level;
    if (va > vb) return -1;
    if (va < vb) return  1;
    return 0;
}

// Print the top-K highest bins and the noise floor (debug aid, verbose only).
static void print_top_peaks(double start_freq, double bin_bw,
                            float *values, int n, int top_k)
{
    if (n <= 0 || top_k <= 0)
        return;

    peak_t *peaks = malloc((size_t)n * sizeof(peak_t));
    if (!peaks)
        return;

    double floor_min = (double)values[0];
    for (int i = 0; i < n; i++)
    {
        peaks[i].idx   = i;
        peaks[i].level = values[i];
        if ((double)values[i] < floor_min)
            floor_min = (double)values[i];
    }

    // Sort descending by level
    qsort(peaks, (size_t)n, sizeof(peak_t), cmp_peak_desc);

    // Noise floor at the 5th percentile
    int floor_idx = (int)((double)n * 0.05 + 0.5);
    if (floor_idx >= n) floor_idx = n - 1;
    double noise_floor = (double)peaks[floor_idx].level;

    int count = (n < top_k) ? n : top_k;
    printf("[FFT] top %d peaks (noise floor: %.1f dBFS, min: %.1f):\n",
           count, noise_floor, floor_min);
    for (int j = 0; j < count; j++)
    {
        double freq_hz = start_freq + (double)peaks[j].idx * bin_bw;
        printf("  #%-2d  %s  %+.1f dBFS\n",
               j + 1,
               print_freq((freq_t)(freq_hz / 1000.0 + 0.5) * 1000),
               (double)peaks[j].level);
    }
    fflush(stdout);

    free(peaks);
}

/* Cluster from the FFT sweep — used to defer verification so that
 * strong signals are verified first (before intermittent transmissions
 * end during long verify chains). */
typedef struct {
    freq_t  peak_freq;  /* center frequency of the strongest bin [Hz] */
    double  peak_level; /* maximum dBFS value in the cluster */
    bool    visited;    /* already verified and locked in this batch */
} fft_cluster_t;

/* Comparator for sorting clusters by descending peak level. */
static int cmp_cluster_desc(const void *a, const void *b)
{
    double pa = ((const fft_cluster_t *)a)->peak_level;
    double pb = ((const fft_cluster_t *)b)->peak_level;
    if (pa > pb) return -1;
    if (pa < pb) return  1;
    return 0;
}

/* Comparator for sorting clusters by ascending frequency. */
static int cmp_cluster_asc(const void *a, const void *b)
{
    freq_t fa = ((const fft_cluster_t *)a)->peak_freq;
    freq_t fb = ((const fft_cluster_t *)b)->peak_freq;
    if (fa < fb) return -1;
    if (fa > fb) return  1;
    return 0;
}

/* Refine a coarse candidate by reading a local high-res FFT around it.
 * radius_hz  : search half-range (±radius_hz around candidate)
 * fine_bw_hz : refinement bin width (e.g. filter_bw / 100)
 * Finds the spectral bump, walks left/right from its peak to the
 * REFINE_EDGE_DB roll-off, and returns the midpoint (centre of the
 * modulated carrier).  Updates *candidate on success. */
static bool RefineFFTPeak(int sockfd, freq_t *candidate,
                           freq_t *out_center,
                           double *out_level,
                           int radius_hz, int fine_bw_hz,
                           freq_t freq_min, freq_t freq_max)
{
    const int refine_n_reads = 3;
    const int refine_delay_us = 10000;
    const float refine_edge_db = 12.0f;
    const int refine_rounding = 100;

    int n_bins = (2 * radius_hz) / fine_bw_hz + 1;
    float *vals = malloc((size_t)n_bins * sizeof(float));
    if (!vals)
        return false;

    /* Accumulate linear power across time-spaced FFT frames.
     * The delay between reads ensures each capture catches an
     * independent modulation state. */
    float *accum = calloc((size_t)n_bins, sizeof(float));
    if (!accum)
    {
        free(vals);
        return false;
    }

    /* Fail-fast: if the candidate is outside the visible (or NCO-safe)
     * range, the refinement request would land outside Gqrx's current
     * spectrum window.  Skip without any I/O. */
    if (*candidate + (freq_t)radius_hz < freq_min ||
        *candidate - (freq_t)radius_hz > freq_max)
    {
        free(accum);
        free(vals);
        return false;
    }

    freq_t rc; double rs = 0, re = 0, rb = 0;
    int rn = 0, nv = 0, n_valid = 0;

    for (int r = 0; r < refine_n_reads; r++)
    {
        if (r > 0)
            usleep(refine_delay_us);

        if (!GetFFTValuesPartial(sockfd,
                    (double)*candidate - radius_hz,
                    n_bins, fine_bw_hz,
                    vals, n_bins,
                    &rc, &rs, &re, &rb, &rn, &nv) || nv <= 0)
            continue;

        // Verify the returned FFT window covers the requested range.
        // Gqrx may silently return data from the visible band when
        // the requested sub-range falls outside the visible spectrum,
        // which would shift the centroid to a different frequency.
        double requested_start = (double)*candidate - radius_hz;
        if (fabs(rs - requested_start) > (double)fine_bw_hz)
            continue;

        for (int j = 0; j < nv; j++)
            accum[j] += powf(10.0f, vals[j] / 10.0f);
        n_valid++;
    }

    if (n_valid < 1)
    {
        free(accum);
        free(vals);
        return false;
    }

    /* Convert integrated power back to dBFS for peak/edge search */
    float inv_n = 1.0f / (float)n_valid;
    for (int j = 0; j < nv; j++)
        vals[j] = 10.0f * log10f(accum[j] * inv_n + 1.0e-20f);

    /* Find the peak bin on the integrated spectrum */
    int best_idx = 0;
    float best_val = vals[0];
    for (int j = 1; j < nv; j++)
        if (vals[j] > best_val)
        {
            best_val = vals[j];
            best_idx = j;
        }

    /* If the global maximum is at a window edge the true peak lies
     * outside — likely bleed from an adjacent strong station.  Fall
     * back to the strongest non-edge bin.  If all bins are at the
     * same level (flat noise) we reject the refinement. */
    if (best_idx == 0 || best_idx == nv - 1)
    {
        int alt_idx  = -1;
        float alt_val = -1e10f;
        int lo = (best_idx == 0) ? 1 : 0;
        int hi = (best_idx == nv - 1) ? nv - 2 : nv - 1;
        for (int j = lo; j <= hi; j++)
            if (vals[j] > alt_val)
            {
                alt_val = vals[j];
                alt_idx = j;
            }

        if (alt_idx < 0 || alt_idx == 0 || alt_idx == nv - 1 ||
            alt_val <= best_val - refine_edge_db)
        {
            if (opt_verbose)
                printf("[FFT] refine edge skip (best_idx=%d, alt_idx=%d, nv=%d)\n",
                       best_idx, alt_idx, nv);

            free(accum);
            free(vals);
            return false;
        }

        best_idx = alt_idx;
        best_val = alt_val;

        if (opt_verbose)
            printf("[FFT] refine edge fallback best_idx=%d best_val=%.1f\n",
                   best_idx, best_val);
    }

    /* Walk left/right to find edges of the spectral bump */
    float floor_db = best_val - refine_edge_db;
    int left = best_idx;
    while (left > 0 && vals[left - 1] > floor_db)
        left--;
    int right = best_idx;
    while (right < nv - 1 && vals[right + 1] > floor_db)
        right++;

    /* Truncated bump — the spectral structure hits the window edge,
     * so the centroid is unreliable (likely bleed from an adjacent
     * strong station).  Reject the refinement. */
    if (left == 0 || right == nv - 1)
    {
        if (opt_verbose)
            printf("[FFT] refine truncated bump (best_idx=%d, left=%d, right=%d, nv=%d)\n",
                   best_idx, left, right, nv);

        free(accum);
        free(vals);
        return false;
    }

    /* Power-weighted centroid over the spectral bump.
     * Uses the integrated linear power directly, which is much
     * more stable than the edge midpoint for modulated signals:
     * a notch at one edge has negligible effect on the centroid
     * whereas it moves the midpoint by half the notch width. */
    double sum_power = 0.0;
    double sum_freq = 0.0;
    for (int j = left; j <= right; j++)
    {
        double f = rs + (double)j * rb;
        sum_power += (double)accum[j];
        sum_freq  += f * (double)accum[j];
    }
    double centre = sum_freq / sum_power;

    *candidate = (freq_t)(centre / (double)refine_rounding + 0.5) * refine_rounding;

    if (opt_verbose)
    {
        printf("[FFT] refine %d/%d dly=%d -> best_idx=%d best_val=%.1f "
               "left=%d right=%d centre=%.0f centroid=%s\n",
               n_valid, refine_n_reads, refine_delay_us,
               best_idx, best_val, left, right, centre,
               print_freq(*candidate));
        fflush(stdout);
    }

    if (out_center)
        *out_center = rc;
    if (out_level)
        *out_level = (double)best_val;

    free(accum);
    free(vals);
    return true;
}

/* Forward declaration — defined below, called by FineTunePeaks. */
static bool VerifyCenterFrequency(int sockfd, int fft_bw,
                                   freq_t fft_center,
                                   freq_t previous_center);

/* Fine-tune all candidate frequencies with sub-bin precision before
 * any SetFreq moves the receiver.  Every refinement reads the same
 * consistent spectrum at the sweep-start centre.
 * If out_center is non-NULL, the F: from the last refine read
 * is written to it (HW LO position, never the VFO frequency).
 *
 * Aborts early if the centre drifts (user moved the HW LO).  Also
 * re-reads the squelch periodically so user changes take effect
 * mid-batch. */
static void
FineTunePeaks(
    int             sockfd,
    fft_cluster_t  *clusters,
    int             n_clusters,
    int             filter_bw,
    freq_t         *out_center,
    freq_t          previous_center,
    freq_t          freq_min,
    freq_t          freq_max,
    double         *squelch)
{
    for (int c = 0; c < n_clusters; c++)
    {
        /* Re-read squelch every 50 candidates so user changes on the
         * Gqrx GUI take effect without waiting for the next sweep. */
        if (c > 0 && c % 50 == 0)
            GetSquelchLevel(sockfd, squelch);

        /* Skip clusters whose peak is below the current squelch —
         * no point spending I/O on below-threshold noise bumps. */
        if (clusters[c].peak_level < *squelch)
            continue;

        freq_t coarse = clusters[c].peak_freq;
        RefineFFTPeak(sockfd, &clusters[c].peak_freq,
                       out_center, &clusters[c].peak_level,
                       filter_bw, filter_bw / 100,
                       freq_min, freq_max);

        /* Abort remaining refinements if the centre drifted — the
         * candidate list is stale for the new centre. */
        if (!VerifyCenterFrequency(sockfd, filter_bw,
                                    *out_center, previous_center))
            break;

        if (opt_verbose && coarse != clusters[c].peak_freq)
        {
            printf("[FFT] refine %s -> %s\n",
                   print_freq(coarse),
                   print_freq(clusters[c].peak_freq));
            fflush(stdout);
        }
    }
}

/* Check whether the FFT centre has drifted from the sweep-time
 * baseline.  Returns false (and prints a message) when the drift
 * exceeds GQRX_CENTER_CHANGE_HZ, indicating the candidate list
 * is stale and the caller should re-read the spectrum.
 *
 * When fft_center is non-zero the caller already has a fresh
 * F: value — no extra I/O.  When 0, the function queries
 * GetFFTParameters.  previous_center is set once per sweep by
 * the caller at next_sweep:. */
static bool
VerifyCenterFrequency(int sockfd, int fft_bw,
                      freq_t fft_center,
                      freq_t previous_center)
{
    freq_t v_center = fft_center;

    if (v_center == 0)
    {
        double dummy_s, dummy_e, dummy_b;
        int dummy_n, dummy_c;

        if (!GetFFTParameters(sockfd, fft_bw,
                              &v_center, &dummy_s, &dummy_e,
                              &dummy_b, &dummy_n, &dummy_c))
            return true;   /* socket issue — let the caller handle it */
    }

    if (previous_center != 0 && v_center != previous_center)
    {
        freq_t drift = (v_center > previous_center)
                     ? v_center - previous_center
                     : previous_center - v_center;
        if (drift >= GQRX_CENTER_CHANGE_HZ)
        {
            if (opt_verbose)
                printf("[FFT] centre changed, aborting verify batch\n");
            return false;
        }
    }

    return true;
}

/* Maximum number of FFT clusters (above-squelch contiguous regions)
 * and the local-maxima array size. */
#define FFT_CLUSTER_MAX 4096

/* Minimum topographic prominence (dB) for a bin to be emitted as a
 * candidate.  3 dB rejects noise bumps while passing real FM stations. */
#define FFT_PEAK_PROMINENCE_DB 3.0

/* Maximum bins per FFT sub-request when reading the spectrum via the
 * "FFT V" protocol.  Each bin is serialised as ~7 bytes of text
 * (" -xx.x").  Keeping sub-requests small avoids large text allocations
 * and slow serialization on Gqrx's side when the visible spectrum is
 * wide (30+ MHz at ~625 Hz resolution = 38k+ bins).
 * Start at 50 for testing (each response ~400 bytes), ramp up after
 * validating chunk alignment. */
#define FFT_CHUNK_BINS 4000

/* CLEAN-style detection on a spectrum slice.
 *
 * Converts the slice to linear power, then iteratively finds the
 * global maximum, subtracts a triangular contribution (width = fft_bw/2),
 * and emits the peak as a candidate.  This repeats until the strongest
 * remaining bin falls below squelch or within 3 dB of the noise floor.
 *
 * Unlike strict-local-maximum detection, this reveals weaker stations
 * hidden on the shoulder of a much stronger neighbour — the subtraction
 * exposes them on subsequent iterations.  The approach is resolution-
 * agnostic and requires no assumption about bin alignment.
 *
 * The triangular model is a coarse approximation of the Hann window's
 * main lobe.  Even a crude subtraction is sufficient to expose the
 * next-strongest signal; the exact window shape is unimportant here.
 *
 * Parameters:
 *   work         linear-power working array (modified in-place)
 *   gw           number of bins in work
 *   squelch_lin  squelch threshold in linear power
 *   gap_min_db   noise floor in dBFS (3 dB floor gate)
 *   radius_bins  triangle half-width in bins, = fft_bw/2 / fb
 *   fs, fb       spectrum start (Hz) and bin width (Hz)
 *   lo           index offset into the parent vals[] array
 *   freq_min, freq_max   frequency range filter
 *   prev         previous candidate freq (merge check)
 *   clusters, max_clusters  output array and its capacity
 *   write_idx    absolute index into clusters[] to write from
 *
 * Returns the number of candidates added. */
static int CleanDetectGap(
    double *work, int gw,
    double squelch_lin, double gap_min_db,
    double radius_bins,
    double fs, double fb, int lo,
    freq_t freq_min, freq_t freq_max,
    freq_t *prev,
    fft_cluster_t *clusters, int max_clusters,
    int write_idx)
{
    int added = 0;

    for (int ci = 0; ci < 100 && write_idx + added < max_clusters; ci++)
    {
        /* Find the strongest remaining bin */
        int ml = 0;
        double mv = work[0];
        for (int jj = 1; jj < gw; jj++)
            if (work[jj] > mv)
            {
                mv = work[jj];
                ml = jj;
            }

        /* Stop when the peak drops below squelch or into the noise */
        double mv_db = 10.0 * log10(mv);
        int j = lo + ml;
        freq_t cf = (freq_t)((fs + (double)j * fb)
                             / 1000.0 + 0.5) * 1000;

        if (mv < squelch_lin)
        {
            break;
        }
        if (mv_db - gap_min_db < 3.0)
        {
            break;
        }

        /* Subtract the triangular model BEFORE any checks.  We always
         * remove this energy so the iteration converges even when the
         * candidate is later rejected (range, merge, ban). */
        for (int jj = 0; jj < gw; jj++)
        {
            double d = abs(jj - ml);
            if (d < radius_bins)
            {
                double contrib = mv * (1.0 - d / radius_bins);
                if (contrib > 0)
                {
                    work[jj] -= contrib;
                    if (work[jj] < 0)
                        work[jj] = 0;
                }
            }
        }

        if (cf < freq_min || cf > freq_max)
        {
            continue;
        }

        /* Merge check: skip if within opt_scan_bw of the previous
         * candidate in this gap (same station detected twice). */
        if (*prev != 0 &&
            (cf > *prev ? cf - *prev : *prev - cf) < opt_scan_bw)
        {
            continue;
        }
        *prev = cf;

        if (IsBannedFreq(&cf))
        {
            continue;
        }

        clusters[write_idx + added].peak_freq  = cf;
        clusters[write_idx + added].peak_level = mv_db;
        clusters[write_idx + added].visited    = false;
        added++;
    }

    return added;
}

/* Read the FFT spectrum at a given resolution, splitting into
 * sub-requests of FFT_CHUNK_BINS each to avoid serialising huge
 * text responses from Gqrx.  Results are placed in accum[] at
 * frequency-derived bin indices to maintain alignment across
 * chunks. */
static bool ReadSpectrum(
    int     sockfd,
    int     bw,
    double  start_hz,
    int     n_bins,
    int     n_reads,
    int     chunk_max,
    float  *accum,
    double *out_fs,
    double *out_fb,
    int    *out_fcount,
    int    *out_n_valid)
{
    /*
     * Read and accumulate the FFT spectrum from Gqrx via the "FFT V"
     * protocol with automatic request chunking and multi-frame averaging.
     *
     * Sends one or more "FFT V S:<start> C:<count> W:<bw>" commands
     * to cover the full [start_hz, start_hz + n_bins * bw) range.
     * When n_reads > 1, accumulates linear power across multiple frames;
     * the caller divides by *out_n_valid and converts to dBFS.
     *
     * Each sub-response is placed in the output array at the bin index
     * derived from its actual returned S: and B: fields so that
     * chunk-to-chunk alignment is frequency-correct even when Gqrx
     * rounds the requested start frequency.
     *
     * Parameters:
     *   sockfd      — Gqrx remote control socket
     *   bw          — W: pooled bin width (Hz)
     *   start_hz    — S: requested first-bin centre frequency
     *   n_bins      — total bins to cover (accum capacity)
     *   n_reads     — frames to average (1 = single, 5 = stabilised)
     *   chunk_max   — max bins per sub-request (≤ n_bins)
     *   accum       — output array (linear power, pre-zeroed, n_bins)
     *   out_fs      — actual start frequency from first chunk
     *   out_fb      — actual bin width from first chunk
     *   out_fcount  — actual bins covered
     *   out_n_valid — number of successful reads (1..n_reads)
     *
     * Returns true if at least one read returned useful data. */
    *out_fs      = 0;
    *out_fb      = 0;
    *out_fcount  = 0;
    *out_n_valid = 0;

    if (chunk_max <= 0)
        chunk_max = n_bins;

    double end_hz = start_hz + (double)n_bins * (double)bw;

    for (int r = 0; r < n_reads; r++)
    {
        if (r > 0)
            usleep(50000);

        float *read_buf = calloc((size_t)n_bins, sizeof(float));
        if (!read_buf)
            continue;

        bool    chunk_ok = true;
        int     last_idx = 0;
        double  next_off = 0;  /* offset from start_hz for the next chunk */

        while (start_hz + next_off < end_hz)
        {
            double c_start = start_hz + next_off;
            int    c_bins  = chunk_max;

            /* Shrink the last chunk if it would overshoot the total range */
            if (c_start + (double)c_bins * (double)bw > end_hz)
            {
                c_bins = (int)((end_hz - c_start) / (double)bw);
                if (c_bins <= 0)
                    break;
            }

            float *tmp = malloc((size_t)c_bins * sizeof(float));
            if (!tmp)
            {
                chunk_ok = false;
                break;
            }

            freq_t  fc;
            double  rs = 0, re = 0, rb = 0;
            int     rn = 0, nv = 0;

            bool ok = GetFFTValuesPartial(
                sockfd, c_start, c_bins, bw,
                tmp, c_bins,
                &fc, &rs, &re, &rb, &rn, &nv);

            if (!ok || nv <= 0)
            {
                free(tmp);
                chunk_ok = false;
                break;
            }

            /* Verify the returned window covers the requested range */
            if (fabs(rs - c_start) > (double)bw)
            {
                free(tmp);
                chunk_ok = false;
                break;
            }

            /*
             * Derive the bin index in the global spectrum from the
             * actual returned S: and B:, not the request parameters.
             * This prevents misalignment when Gqrx rounds the requested
             * start frequency or returns a slightly different bin width.
             *
             * Example: request S:1000000 B:625, Gqrx returns S:1000312.5.
             *   idx_d = (1000312.5 - 1000000) / 625 = 0.5
             *   bin = round(0.5) = 1   ← bin 1, not bin 0
             * This puts the data at the correct frequency-derived slot. */
            double idx_d = (rs - start_hz) / rb;
            int    bin   = (int)round(idx_d);
            int    expected = (int)(next_off / (double)bw);

            /*
             * Safety guard: if the returned start frequency drifted by
             * more than one bin from the flat grid, discard this chunk
             * to avoid gaps or overlaps in the output spectrum. */
            if (abs(bin - expected) > 1)
            {
                free(tmp);
                chunk_ok = false;
                break;
            }

            int copy = nv;
            int src_offset = 0;

            /* If rs rounded to a bin before the start of our buffer,
             * shift the source offset and reduce the copy count so we
             * don't underrun read_buf. */
            if (bin < 0)
            {
                src_offset = -bin;
                copy      -= src_offset;
                bin        = 0;
            }

            if (bin + copy > n_bins)
                copy = n_bins - bin;
            if (copy > 0)
            {
                for (int j = 0; j < copy; j++)
                    read_buf[bin + j] = tmp[src_offset + j];
                if (bin + copy > last_idx)
                    last_idx = bin + copy;
            }

            free(tmp);

            /* Advance to the next chunk based on the actual returned
             * geometry so we don't skip or overlap when Gqrx returns
             * fewer bins than requested. */
            next_off = (rs + (double)nv * rb) - start_hz;
        }

        if (chunk_ok && last_idx > 0)
        {
            for (int j = 0; j < last_idx; j++)
                accum[j] += powf(10.0f, read_buf[j] / 10.0f);

            (*out_n_valid)++;

            if (*out_n_valid == 1)
            {
                *out_fs     = start_hz;
                *out_fb     = (double)bw;
                *out_fcount = last_idx;
            }
        }

        free(read_buf);
    }

    return *out_n_valid > 0;
}

/* Scan gaps between coarse-pass candidates at fine resolution to
 * collect peaks the coarse pass missed (stations between filter-width
 * bins or masked by adjacent strong signals).  Gap margins prevent
 * re-detecting stations already found in the coarse pass. */
static void
CollectFinePeaks(
    int             sockfd,
    fft_cluster_t  *clusters,
    int            *n_clusters,
    double          squelch,
    freq_t          freq_min,
    freq_t          freq_max,
    double          fft_start,
    double          fft_end,
    int             filter_bw)
{
    if (*n_clusters <= 0)
        return;

    /* Sort coarse candidates by frequency for gap scanning.  The
     * caller re-sorts by peak level afterward. */
    qsort(clusters, (size_t)*n_clusters, sizeof(fft_cluster_t), cmp_cluster_asc);

    /* Read the full spectrum at fine resolution with multi-frame
     * averaging.  A single FFT frame can misrepresent a modulated
     * station that happens to be at a low point, or Gqrx may return
     * data slightly misaligned.  ReadSpectrum handles chunking,
     * alignment, and 5-read averaging internally. */
    int    fine_bw = filter_bw / 16;
    int    nbins   = (int)((fft_end - fft_start) / fine_bw) + 1;
    float *vals    = malloc((size_t)nbins * sizeof(float));
    float *accum   = calloc((size_t)nbins, sizeof(float));
    if (!vals || !accum)
    {
        free(vals);
        free(accum);
        return;
    }

    double fs = 0, fb = 0;
    int    fcount = 0, n_valid = 0;

    if (!ReadSpectrum(sockfd, fine_bw, fft_start, nbins, 5, FFT_CHUNK_BINS,
                       accum, &fs, &fb, &fcount, &n_valid))
    {
        free(vals);
        free(accum);
        return;
    }

    /* Convert accumulated linear power back to dBFS */
    float inv_n = 1.0f / (float)n_valid;
    for (int j = 0; j < fcount; j++)
        vals[j] = 10.0f * log10f(accum[j] * inv_n + 1.0e-20f);
    free(accum);

    freq_t half_bw = (freq_t)filter_bw / 2;
    freq_t prev    = 0;
    int    added   = 0;

    /* Scan gap before the first coarse candidate. */
    freq_t gap_lo = (freq_t)fft_start;
    freq_t gap_hi = clusters[0].peak_freq > half_bw
                    ? clusters[0].peak_freq - half_bw : (freq_t)fft_start;

    if (gap_lo < gap_hi)
    {
        int lo = (int)((gap_lo - fs) / fb + 0.5);
        int hi = (int)((gap_hi - fs) / fb + 0.5);
        if (lo < 0) lo = 0;
        if (hi >= fcount) hi = fcount - 1;
        int gw = hi - lo + 1;
        if (gw >= 1)
        {
            double *work = malloc((size_t)gw * sizeof(double));
            double squelch_lin = pow(10.0, squelch / 10.0);
            double gap_min = 1e10;
            for (int jj = 0; jj < gw; jj++)
            {
                double v_db = (double)vals[lo + jj];
                work[jj] = pow(10.0, v_db / 10.0);
                if (v_db < gap_min) gap_min = v_db;
            }

            added += CleanDetectGap(work, gw,
                squelch_lin, gap_min,
                (double)filter_bw / 2.0 / fb,
                fs, fb, lo,
                freq_min, freq_max,
                &prev,
                clusters, FFT_CLUSTER_MAX, *n_clusters + added);

            free(work);
        }
    }

    /* Scan gaps between consecutive coarse candidates. */
    for (int c = 1; c < *n_clusters && *n_clusters + added < FFT_CLUSTER_MAX; c++)
    {
        gap_lo = clusters[c - 1].peak_freq + half_bw;
        gap_hi = clusters[c].peak_freq > half_bw
                 ? clusters[c].peak_freq - half_bw : 0;

        if (gap_lo >= gap_hi) continue;

        int lo = (int)((gap_lo - fs) / fb + 0.5);
        int hi = (int)((gap_hi - fs) / fb + 0.5);
        if (lo < 0) lo = 0;
        if (hi >= fcount) hi = fcount - 1;
        if (lo > hi) continue;
        int gw = hi - lo + 1;
        if (gw < 1) continue;

        double *work = malloc((size_t)gw * sizeof(double));
        double squelch_lin = pow(10.0, squelch / 10.0);
        double gap_min = 1e10;
        for (int jj = 0; jj < gw; jj++)
        {
            double v_db = (double)vals[lo + jj];
            work[jj] = pow(10.0, v_db / 10.0);
            if (v_db < gap_min) gap_min = v_db;
        }

        added += CleanDetectGap(work, gw,
            squelch_lin, gap_min,
            (double)filter_bw / 2.0 / fb,
            fs, fb, lo,
            freq_min, freq_max,
            &prev,
            clusters, FFT_CLUSTER_MAX, *n_clusters + added);

        free(work);
    }

    /* Scan gap after the last coarse candidate. */
    if (*n_clusters + added < FFT_CLUSTER_MAX)
    {
        gap_lo = clusters[*n_clusters - 1].peak_freq + half_bw;
        gap_hi = (freq_t)fft_end;

        if (gap_lo < gap_hi)
        {
            int lo = (int)((gap_lo - fs) / fb + 0.5);
            int hi = (int)((gap_hi - fs) / fb + 0.5);
            if (lo < 0) lo = 0;
            if (hi >= fcount) hi = fcount - 1;
            int gw = hi - lo + 1;
            if (gw >= 1)
            {
                double *work = malloc((size_t)gw * sizeof(double));
                double squelch_lin = pow(10.0, squelch / 10.0);
                double gap_min = 1e10;
                for (int jj = 0; jj < gw; jj++)
                {
                    double v_db = (double)vals[lo + jj];
                    work[jj] = pow(10.0, v_db / 10.0);
                    if (v_db < gap_min) gap_min = v_db;
                }

                added += CleanDetectGap(work, gw,
                    squelch_lin, gap_min,
                    (double)filter_bw / 2.0 / fb,
                    fs, fb, lo,
                    freq_min, freq_max,
                    &prev,
                    clusters, FFT_CLUSTER_MAX, *n_clusters + added);

                free(work);
            }
        }
    }

    *n_clusters += added;

    if (opt_verbose && added > 0)
    {
        printf("[FFT] second pass: %d additional candidate(s) at %d Hz resolution\n",
               added, fine_bw);
        fflush(stdout);
    }

    free(vals);
}

/* Sort + collapse clusters within opt_scan_bw of each other, keeping
 * the higher peak.  This eliminates duplicates from adjacent coarse
 * or fine bins that belong to the same station. */
static void MergeClusters(fft_cluster_t *clusters, int *n_clusters, freq_t filter_bw)
{
    if (*n_clusters <= 1)
        return;

    qsort(clusters, (size_t)*n_clusters, sizeof(fft_cluster_t), cmp_cluster_asc);

    int dst = 0;
    for (int src = 1; src < *n_clusters; src++)
    {
        if (clusters[src].peak_freq >= clusters[dst].peak_freq &&
            clusters[src].peak_freq - clusters[dst].peak_freq < filter_bw)
        {
            if (clusters[src].peak_level > clusters[dst].peak_level)
                clusters[dst] = clusters[src];
        }
        else
        {
            dst++;
            if (dst != src)
                clusters[dst] = clusters[src];
        }
    }
    *n_clusters = dst + 1;
}

/* Walk all FFT bins, identify contiguous above-squelch regions,
 * find strict local maxima, compute topographic prominence, and emit
 * candidates that pass range and banned-list filters. */
static void CollectCoarsePeaks(
    float          *values,
    int             n_vals,
    double          squelch,
    freq_t          freq_min,
    freq_t          freq_max,
    double          fft_start,
    double          fft_bin,
    fft_cluster_t  *clusters,
    int             max_clusters,
    int            *n_clusters)
{
    int i = 0;
    freq_t prev_candidate = 0;
    *n_clusters = 0;

    while (i < n_vals && *n_clusters < max_clusters)
    {
        if (values[i] < squelch)
        {
            i++;
            continue;
        }

        /* Collect the contiguous above-squelch region */
        int cs = i;
        while (i < n_vals && values[i] >= squelch)
            i++;
        int ce = i;  // exclusive

        if (opt_verbose)
        {
            double bin_freq = fft_start + (double)cs * fft_bin;
            printf("[FFT] cluster starts at bin[%d] freq=%.0f val=%.1f sq=%.1f\n",
                   cs, bin_freq, (double)values[cs], squelch);
            fflush(stdout);
        }

        /* Find all local maxima within [cs, ce).  A bin is a local
         * maximum when it is strictly higher than both neighbours;
         * this prevents flat/plateau regions from emitting multiple
         * candidates. */
        int locmax_idx[max_clusters];
        double locmax_level[max_clusters];
        int n_locmax = 0;

        for (int j = cs; j < ce && n_locmax < max_clusters; j++)
        {
            bool left_ok  = (j == cs)      || (double)values[j] > (double)values[j-1];
            bool right_ok = (j == ce - 1)  || (double)values[j] > (double)values[j+1];
            if (left_ok && right_ok)
            {
                locmax_idx[n_locmax]   = j;
                locmax_level[n_locmax] = (double)values[j];
                n_locmax++;
            }
        }

        /* If the region is monotonic (no local maxima), fall back to
         * emitting the single strongest bin. */
        if (n_locmax < 1)
        {
            int best_j = cs;
            double best_val = (double)values[cs];
            for (int j = cs + 1; j < ce; j++)
            {
                double v = (double)values[j];
                if (v > best_val)
                {
                    best_val = v;
                    best_j = j;
                }
            }
            locmax_idx[0]   = best_j;
            locmax_level[0] = best_val;
            n_locmax = 1;
        }

        /* Emit one candidate per local maximum whose topographic
         * prominence (drop to the shallower adjacent valley) exceeds
         * FFT_PEAK_PROMINENCE_DB.  3 dB rejects noise bumps while
         * easily passing real FM stations separated by 400+ kHz. */
        for (int p = 0; p < n_locmax; p++)
        {
            int   midx = locmax_idx[p];
            double pk  = locmax_level[p];

            /* Deepest valley between this peak and the adjacent peak
             * (or the cluster edge if first/last in the region).
             * Initialise to squelch — for a peak at the cluster edge
             * the valley outside is below the squelch threshold, so
             * squelch is the natural floor.
             *
             * When squelch is set far below the actual spectrum
             * (e.g. -150 dBFS to disable squelch), this default
             * inflates prominence for every noise bump.  As a
             * safety net, peek at the bin just outside the cluster
             * boundary and use it if it is higher (less negative)
             * than squelch, giving us the real noise floor. */
            int left_bound = (p > 0) ? locmax_idx[p-1] : cs;
            double min_left = squelch;
            for (int j = left_bound; j < midx; j++)
            {
                double v = (double)values[j];
                if (v < min_left)
                    min_left = v;
            }
            if (p == 0 && cs == midx && cs > 0)
            {
                double edge = (double)values[cs - 1];
                if (edge > min_left)
                    min_left = edge;
            }

            /* Deepest valley to the right. */
            int right_bound = (p < n_locmax - 1) ? locmax_idx[p+1] : ce - 1;
            double min_right = squelch;
            for (int j = midx + 1; j <= right_bound; j++)
            {
                double v = (double)values[j];
                if (v < min_right)
                    min_right = v;
            }
            if (p == n_locmax - 1 && ce - 1 == midx && ce < n_vals)
            {
                double edge = (double)values[ce];
                if (edge > min_right)
                    min_right = edge;
            }

            /* Key col = shallower of the two valleys (higher minimum).
             * Prominence = peak level above the key col. */
            double key_col = (min_left > min_right) ? min_left : min_right;
            double prominence = pk - key_col;

            if (prominence < FFT_PEAK_PROMINENCE_DB)
            {
                if (opt_verbose)
                {
                    printf("[FFT] skip peak at bin[%d] %.0f Hz"
                           " (prominence %.1f dB < %.0f dB)\n",
                           midx, fft_start + (double)midx * fft_bin,
                           prominence, FFT_PEAK_PROMINENCE_DB);
                    fflush(stdout);
                }
                continue;
            }

            freq_t candidate = (freq_t)((fft_start + (double)midx * fft_bin)
                                        / 1000.0 + 0.5) * 1000;

            if (candidate < freq_min || candidate > freq_max)
            {
                if (opt_verbose)
                    printf("[FFT] skip out-of-range %s (%.0f-%.0f)\n",
                           print_freq(candidate),
                           (double)freq_min, (double)freq_max);
                continue;
            }

            if (prev_candidate != 0 &&
                (candidate > prev_candidate ?
                 candidate - prev_candidate :
                 prev_candidate - candidate) < opt_scan_bw)
            {
                if (opt_verbose)
                {
                    printf("[FFT] merge skip %s (prev=%s, dist < %llu)\n",
                           print_freq(candidate),
                           print_freq(prev_candidate),
                           opt_scan_bw);
                    fflush(stdout);
                }
                continue;
            }
            prev_candidate = candidate;

            if (IsBannedFreq(&candidate))
            {
                if (opt_verbose)
                {
                    printf("[FFT] candidate %s banned, skipping\n",
                           print_freq(candidate));
                    fflush(stdout);
                }
                continue;
            }

            clusters[*n_clusters].peak_freq   = candidate;
            clusters[*n_clusters].peak_level  = pk;
            clusters[*n_clusters].visited     = false;
            (*n_clusters)++;
        }
    }
}

/* Sort candidates by peak level descending so the strongest signals
 * are verified first.  Prints the candidate list if opt_verbose. */
static void SortCandidatesByLevel(
    fft_cluster_t *clusters,
    int n_clusters,
    double squelch)
{
    qsort(clusters, (size_t)n_clusters, sizeof(fft_cluster_t), cmp_cluster_desc);

    if (opt_verbose && n_clusters > 0)
    {
        printf("[FFT] %d candidates (bins >= %.1f dBFS):\n",
               n_clusters, squelch);
        for (int c = 0; c < n_clusters; c++)
        {
            printf("  #%d  %s  peak=%.1f dBFS\n",
                   c + 1,
                   print_freq(clusters[c].peak_freq),
                   clusters[c].peak_level);
        }
        fflush(stdout);
    }
}

/* RefreshCandidates — between-candidate refresh of the candidate list.
 *
 * Called from ProcessCandidates after each candidate completes its lock
 * period.  Reads a fresh FFT spectrum, runs coarse peak detection, and
 * diffs against the unvisited candidates in the list.  Existing candidates
 * whose frequency no longer has an above-squelch peak are removed.  New
 * strong peaks not matching any existing candidate are refined and added.
 * The list is re-sorted by level descending.
 *
 * Parameters:
 *   sockfd     — connected Gqrx socket
 *   clusters   — [in/out] candidate array (mutated in place)
 *   n_clusters — [in/out] number of candidates
 *   squelch    — current squelch level (dBFS)
 *   freq_min   — low end of the scan range
 *   freq_max   — high end of the scan range
 *   filter_bw  — channel filter bandwidth (Hz); also used as match tolerance
 */
static void RefreshCandidates(
    int             sockfd,
    fft_cluster_t  *clusters,
    int            *n_clusters,
    double          squelch,
    freq_t          freq_min,
    freq_t          freq_max,
    int             filter_bw)
{
    freq_t center        = 0;
    double fft_start     = 0;
    double fft_end       = 0;
    double fft_bin       = 0;
    int    total         = 0;
    int    count         = 0;

    /* Read fresh FFT geometry and spectrum. */
    if (!GetFFTParameters(sockfd, filter_bw,
                          &center, &fft_start, &fft_end,
                          &fft_bin, &total, &count))
        return;

    if (total <= 0 || count <= 0)
        return;

    float *values = malloc((size_t)total * sizeof(float));
    if (!values)
        return;

    if (!GetFFTValues(sockfd, filter_bw,
                      values, total,
                      &center, &fft_start, &fft_end,
                      &fft_bin, &total, &count))
    {
        free(values);
        return;
    }

    /* Detect current above-squelch peaks. */
    fft_cluster_t current_peaks[FFT_CLUSTER_MAX];
    int n_current = 0;

    CollectCoarsePeaks(values, count, squelch,
                       freq_min, freq_max,
                       fft_start, fft_bin,
                       current_peaks, FFT_CLUSTER_MAX, &n_current);

    free(values);

    /* Match tolerance: signals within filter_bw Hz are the same station. */
    freq_t tolerance = (freq_t)filter_bw;

    /* Compact: keep visited clusters + unvisited clusters that still
     * have a peak in the fresh spectrum. */
    int write = 0;
    for (int i = 0; i < *n_clusters; i++)
    {
        if (clusters[i].visited)
        {
            clusters[write++] = clusters[i];
            continue;
        }

        /* Check if this candidate's frequency still has a peak. */
        bool found = false;
        for (int j = 0; j < n_current; j++)
        {
            freq_t freq_cur = clusters[i].peak_freq;
            freq_t freq_fft = current_peaks[j].peak_freq;
            freq_t delta = (freq_cur > freq_fft)
                         ? freq_cur - freq_fft
                         : freq_fft - freq_cur;
            if (delta <= tolerance)
            {
                found = true;
                break;
            }
        }

        if (found)
            clusters[write++] = clusters[i];
        /* else: signal no longer present — drop it. */
    }
    *n_clusters = write;

    /* Add new peaks not matching any existing cluster (visited or not). */
    for (int j = 0; j < n_current && *n_clusters < FFT_CLUSTER_MAX; j++)
    {
        bool matched = false;
        for (int i = 0; i < *n_clusters; i++)
        {
            freq_t freq_peak = current_peaks[j].peak_freq;
            freq_t freq_cls  = clusters[i].peak_freq;
            freq_t delta = (freq_peak > freq_cls)
                         ? freq_peak - freq_cls
                         : freq_cls - freq_peak;
            if (delta <= tolerance)
            {
                matched = true;
                break;
            }
        }

        if (!matched)
        {
            /* New candidate — refine to sub-bin precision before adding. */
            freq_t refined = current_peaks[j].peak_freq;
            freq_t out_center = 0;
            double refined_level = current_peaks[j].peak_level;

            if (RefineFFTPeak(sockfd, &refined, &out_center, &refined_level,
                              filter_bw, filter_bw / 100,
                              freq_min, freq_max))
            {
                clusters[*n_clusters].peak_freq  = refined;
                clusters[*n_clusters].peak_level = refined_level;
                clusters[*n_clusters].visited    = false;
                (*n_clusters)++;
            }
        }
    }

    /* Re-sort the updated list by descending level. */
    SortCandidatesByLevel(clusters, *n_clusters, squelch);
}

/* Tune to each candidate in descending peak-level order, verifying
 * with the audio-path signal level.  Saves/records/writes hits.
 * squelch is re-read per candidate and updated in-place.
 * Returns true if at least one candidate was accepted.
 * Returns early with partial results if the centre changed. */
static bool ProcessCandidates(
    int             sockfd,
    fft_cluster_t  *clusters,
    int            *n_clusters,
    double         *squelch,
    freq_t          freq_min,
    freq_t          freq_max,
    freq_t          previous_center,
    int             filter_bw)
{
    bool found_any = false;
    int consecutive_rejects = 0;
    int c = 0;

    while (c < *n_clusters)
    {
        /* Stop once the tail is all rejects — remaining candidates
         * are even weaker (sorted descending) and will also fail. */
        if (consecutive_rejects >= 5)
        {
            if (opt_verbose)
                printf("[FFT] %d consecutive rejects, stopping\n",
                       consecutive_rejects);
            break;
        }

        /* Skip already-visited candidates (consumed by an earlier
         * iteration or rejected/out-of-range). */
        if (clusters[c].visited)
        {
            c++;
            continue;
        }

        freq_t  candidate  = clusters[c].peak_freq;
        double  peak_level = clusters[c].peak_level;

        CheckUserInput();
        if (g_socket_dead)
            break;

        if (candidate < freq_min || candidate > freq_max)
        {
            clusters[c].visited = true;
            c++;
            continue;
        }

        /* Skip if the FFT peak is already below the current squelch.
         * The squelch may have been raised since the sweep — no point
         * tuning to a signal that won't pass the audio-path check. */
        if (peak_level < *squelch)
        {
            clusters[c].visited = true;
            consecutive_rejects++;
            c++;
            continue;
        }

        /* Verify the FFT centre has not drifted since the sweep
         * (user retuned the HW LO).  If it has, the candidate list
         * is stale — abort this batch before tuning. */
        if (!VerifyCenterFrequency(sockfd, filter_bw, 0, previous_center))
            return found_any;

        found_any = true;

        if (opt_verbose)
        {
            printf("[FFT] tuning to %s for verify\n",
                   print_freq(candidate));
            fflush(stdout);
        }

        SetFreq(sockfd, candidate);
#ifndef OSX
        if (opt_vox) VoxAudioReset();
#endif
        usleep(g_settle_time_us);

        /* Re-read the squelch level for each candidate so the user can
         * adjust it on the Gqrx GUI during a long verify chain.
         * Without this, a sweep that started with squelch = -150
         * (wide open) would accept noise peaks for every candidate. */
        GetSquelchLevel(sockfd, squelch);

        double level = 0;
        GetSignalLevelEx(sockfd, &level, 5, false);

        if (opt_verbose)
        {
            printf("[FFT] verify %s fft_peak=%.1f audio_level=%.1f sq=%.1f %s\n",
                   print_freq(candidate), peak_level,
                   level, *squelch,
                   (level >= *squelch) ? "ACCEPT" : "REJECT");
            fflush(stdout);
        }

        if (level < *squelch)
        {
            // FFT saw something during the sweep but the audio path
            // disagrees — false positive, skip.
            clusters[c].visited = true;
            consecutive_rejects++;
            c++;
            continue;
        }

        /* Successful verify — reset the reject streak. */
        consecutive_rejects = 0;

        if (opt_record)
            StartRecording(sockfd);

        SaveFreq(candidate);

        char timestamp[BUFSIZE] = {0};
        struct timeval hit_time = GetTime(timestamp);
        printf("[%s] Freq: %s active, Level: %2.2f/%2.2f ",
               timestamp, print_freq(candidate),
               peak_level, *squelch);
        fflush(stdout);

        freq_t dummy_freq = candidate;
        WaitUserInputOrDelay(opt_delay, &dummy_freq);

        DiffTime(timestamp, &hit_time);
        if (opt_record)
            StopRecording(sockfd);
        printf(" [elapsed time %s]\n", timestamp);
        fflush(stdout);

        /* Between-candidate refresh: re-read the spectrum and update
         * the candidate list.  Mark this candidate as visited so the
         * refresh preserves it (it was consumed).  Then restart from
         * the strongest unvisited candidate. */
        clusters[c].visited = true;
        RefreshCandidates(sockfd, clusters, n_clusters, *squelch,
                          freq_min, freq_max, filter_bw);
        c = 0;
    }

    return found_any;
}

//
// ScanFrequenciesInRangeFFT
//
// FFT-based frequency scanner.  Uses the Gqrx remote-control FFT
// extension to read the full visible spectrum in one request, then
// scans all bins for signals above the squelch level.  The FFT uses
// W:<channel_filter_bw> so each pooled bin matches the audio-path
// bandwidth, making dBFS values directly comparable.  No pane
// sweeping — only the currently visible spectrum is analyzed.
// Candidates are verified with the audio-path signal level before
// locking (dual verification).
//
// The scan has two passes:
//   1. Coarse pass — filter-width bins, strict local maxima,
//      topographic prominence filter.
//   2. Fine pass — half-bandwidth bins scanned in the gaps between
//      coarse candidates, detecting stations that fall between
//      coarse bins and are masked by adjacent strong signals.
//
bool ScanFrequenciesInRangeFFT(freq_t freq_min, freq_t freq_max)
{
    double squelch = 0;

    // Use the channel filter bandwidth as the FFT pooled-bin width.
    freq_t filter_bw = 10000;
    GetFilterBandwidth(g_sockfd, &filter_bw);

    // Query FFT parameters to get visible spectrum geometry
    freq_t fft_center = 0;
    double fft_start = 0, fft_end = 0, fft_bin = 0;
    int fft_total = 0, fft_count = 0;

    if (!GetFFTParameters(g_sockfd, filter_bw,
                          &fft_center, &fft_start, &fft_end,
                          &fft_bin, &fft_total, &fft_count))
    {
        fprintf(stderr, "Error: Gqrx does not support the FFT extension.\n");
        return false;
    }

    if (opt_verbose)
    {
        printf("[FFT] initial params: F=%llu S=%.0f E=%.0f B=%.4f N=%d filter_bw=%llu\n",
               fft_center, fft_start, fft_end, fft_bin, fft_total, filter_bw);
        fflush(stdout);
    }

    // Allocate values array
    int max_bins = fft_total;
    float *values = malloc((size_t)max_bins * sizeof(float));
    if (!values)
    {
        fprintf(stderr, "Error: out of memory for FFT values\n");
        return false;
    }

    freq_t previous_center = 0;

    while (true)
    {
        /* Save this sweep's centre as the baseline for the next cycle. */
        previous_center = fft_center;

        // Minimal delay between FFT sweeps — the spectrum is one-shot.
        usleep(GQRX_FFT_UPDATE_US);
#ifdef TESTING_BUILD
        if (g_testing_max_full_sweeps >= 0 &&
            g_testing_sweep_full_count >= g_testing_max_full_sweeps)
            break;
#endif

        CheckUserInput();

        if (g_socket_dead)
            Reconnect();

        //
        // Refresh live parameters at the top of every sweep cycle.
        // The user may have changed squelch, filter bandwidth, or
        // receiver tuning on the Gqrx GUI.
        //
        GetSquelchLevel(g_sockfd, &squelch);
        filter_bw = 10000;
        GetFilterBandwidth(g_sockfd, &filter_bw);

        if (GetFFTParameters(g_sockfd, filter_bw,
                             &fft_center, &fft_start, &fft_end,
                             &fft_bin, &fft_total, &fft_count))
        {
            if (fft_total > max_bins)
            {
                float *nv = realloc(values, (size_t)fft_total * sizeof(float));
                if (nv)
                {
                    values = nv;
                    max_bins = fft_total;
                }
            }

            if (opt_verbose)
            {
                printf("[FFT] params updated: filter_bw=%llu F=%llu B=%.4f N=%d\n",
                       filter_bw, fft_center, fft_bin, fft_total);
                fflush(stdout);
            }
        }

        //
        // Read the full visible spectrum at the current frequency.
        //
        if (!GetFFTValues(g_sockfd, filter_bw,
                          values, max_bins,
                          &fft_center, &fft_start, &fft_end,
                          &fft_bin, &fft_total, &fft_count))
        {
            if (opt_verbose)
                printf("[FFT] read error\n");
            if (g_socket_dead)
                continue;
            usleep(100000);
            continue;
        }

        if (fft_count <= 0)
        {
            usleep(100000);
            continue;
        }

        /* Update the NCO safe range based on Gqrx's current center.
         * This runs once per sweep before any SetFreq changes the
         * tune — only the sweep-level FFT Q is authoritative. */
        if (!has_range)
        {
            freq_t new_min = 0, new_max = 0;
            GetSafeRange(&new_min, &new_max, &fft_center);
            if (new_max > new_min)
            {
                freq_min = new_min;
                freq_max = new_max;
            }
            else
            {
                /* GetSafeRange failed (FFT Q unavailable).  Fall back to
                 * the full visible spectrum — no NCO zone protection,
                 * but better than filtering every candidate to zero. */
                freq_min = (freq_t)fft_start;
                freq_max = (freq_t)fft_end;
            }
        }

        /* Verify the FFT centre has not drifted from the sweep
         * baseline (uses the fresh FFT V center — no extra I/O). */
        if (!VerifyCenterFrequency(g_sockfd, filter_bw,
                                     fft_center, previous_center))
            continue;

        if (opt_verbose)
        {
            printf("[FFT] F=%llu S=%.0f E=%.0f B=%.4f C=%d first=%.1f last=%.1f\n",
                   fft_center, fft_start, fft_end, fft_bin, fft_count,
                   values[0], values[fft_count-1]);
            fflush(stdout);
        }

        //
        // First pass: collect coarse peaks from the sweep spectrum.
        //
        fft_cluster_t clusters[FFT_CLUSTER_MAX];
        int n_clusters = 0;

        CollectCoarsePeaks(values, fft_count, squelch, freq_min, freq_max,
                           fft_start, fft_bin,
                           clusters, FFT_CLUSTER_MAX, &n_clusters);

        //
        // Second pass: collect peaks missed by the coarse pass.
        //
        CollectFinePeaks(g_sockfd, clusters, &n_clusters, squelch,
                         freq_min, freq_max, fft_start, fft_end, filter_bw);

        //
        // Merge coarse and fine candidates within the FFT bin width.
        //
        MergeClusters(clusters, &n_clusters, filter_bw);

        //
        // Fine-tune all candidate frequencies with sub-bin precision.
        //
        FineTunePeaks(g_sockfd, clusters, n_clusters, filter_bw,
                      &fft_center, previous_center,
                      freq_min, freq_max, &squelch);

        /* Verify the FFT centre hasn't drifted during refinement.
         * Uses the updated centre from the last refine FFT read
         * (zero extra I/O).  previous_center is the sweep baseline. */
        if (!VerifyCenterFrequency(g_sockfd, filter_bw,
                                     fft_center, previous_center))
            continue;

        //
        // Sort candidates by peak level descending.
        //
        SortCandidatesByLevel(clusters, n_clusters, squelch);

        //
        // Final pass: verify and save each candidate.
        //
        ProcessCandidates(
            g_sockfd, clusters, &n_clusters, &squelch,
            freq_min, freq_max, previous_center, filter_bw);
    }

    free(values);
    return true;
}

//
// Save frequency found
//
bool SaveFreq(freq_t freq_current)
{
    bool found = false;
    freq_t tollerance = 5000; // 5Khz tolerance (±5 KHz)

    int  temp_count = 0;
    freq_t temp_delta = tollerance;
    int  temp_i     = 0;

    for (int i = 0; i < SavedFreq_Max; i++)
    {
        // Find a previous hit with some tollerance
        if (freq_current >= (SavedFrequencies[i].freq - tollerance) &&
            freq_current <  (SavedFrequencies[i].freq + tollerance)   )
        {
            // found match, loop for minimum delta
            // Use absolute value to handle both positive and negative differences
            freq_t delta;
            if (freq_current >= SavedFrequencies[i].freq)
                delta = freq_current - SavedFrequencies[i].freq;
            else
                delta = SavedFrequencies[i].freq - freq_current;
            
            if ( delta < temp_delta )
            {
                // Found a better match
                temp_delta = delta;
                temp_count = SavedFrequencies[i].count;
                temp_i     = i;
            }
            found = true;
        }
    }
    if (!found)
    {
        SavedFrequencies[SavedFreq_Max].freq  = freq_current;
        SavedFrequencies[SavedFreq_Max].count = 1;
        SavedFrequencies[SavedFreq_Max].miss  = 0;
        SavedFreq_Max++;
        if (SavedFreq_Max >= SAVED_FREQ_MAX)
        {
            SavedFreq_Max = 0;
            memset(SavedFrequencies, 0, sizeof(SavedFrequencies));
        }
        return true;
    }


    // calculate a better one for the next time
    int count = temp_count;
    SavedFrequencies[temp_i].freq = (( ((freq_t)SavedFrequencies[temp_i].freq * count ) + freq_current ) / (count + 1));
    SavedFrequencies[temp_i].count++;
    SavedFrequencies[temp_i].miss = 0;// reset miss count


    return true;
}

//
// Ban a frequency found
//
bool BanFreq (freq_t freq_current)
{
    int i = BannedFreq_Max;
    if (i >= SAVED_FREQ_MAX)
        return false;

    BannedFrequencies[i].freq  = freq_current;
    BannedFreq_Max++;

    for (i = 0; i < SavedFreq_Max; i++)
    {
        // Find a previous hit with some tollerance
        if (freq_current >= (SavedFrequencies[i].freq - g_ban_tollerance) &&
            freq_current <  (SavedFrequencies[i].freq + g_ban_tollerance)   )
        {
            SavedFrequencies[i].count = 0;
        }
    }

    return true;
}
//
// ClearAllBans
//
void ClearAllBans ( void )
{
    BannedFreq_Max = 0; // quick and dirty
}

//
// IsBannedFreq
// Test whether a frequency is banned or not
//
bool IsBannedFreq (freq_t *freq_current)
{
    int i;
    for (i = 0; i < BannedFreq_Max; i++)
    {
        if (*freq_current >= (BannedFrequencies[i].freq - g_ban_tollerance) &&
            *freq_current <  (BannedFrequencies[i].freq + g_ban_tollerance)   )
        {
            // scanning
            *freq_current+= (g_ban_tollerance * 2); // avoid jumping neearby a carrier
            // round up to next near tenth of khz  145892125 -> 145900000
            *freq_current = ceil( *freq_current / 10000.0 ) * 10000.0;
            IsBannedFreq (freq_current);
            return true;
        }
    }

    return false;
}


//
// Debounce
// Waits 300 ms then re-checks the signal level to distinguish a real
// carrier from a transient noise spike that briefly opened the squelch.
//
// A 3 dB hysteresis margin (DEBOUNCE_HYSTERESIS_DB) is applied so that
// a marginal carrier sitting only a couple of dB above the squelch level
// is not rejected by a momentary natural fluctuation.  Without this
// margin, the 300 ms sleep gives enough time for a weak-but-real carrier
// to dip below squelch and trigger an unnecessary backtrack loop.
//
// Only a true signal loss (level drops more than 3 dB below squelch)
// returns false.
//
#define DEBOUNCE_HYSTERESIS_DB 3.0

bool Debounce (freq_t current_freq, double level)
{
    double current_level = level;
    double squelch;
    usleep(300000); // 300 ms wait, hope it's good enough
    GetSignalLevelEx( g_sockfd, &current_level, 5, false );
    GetSquelchLevel ( g_sockfd, &squelch );

    // Apply hysteresis: allow the signal to dip up to DEBOUNCE_HYSTERESIS_DB
    // below squelch before declaring it lost.  This prevents the backtrack
    // oscillation loop caused by marginal carriers fluctuating ±2-3 dB.
    if (current_level < squelch - DEBOUNCE_HYSTERESIS_DB)
        return false; // signal genuinely lost or ghost
    else
        return true;

}

//
// BacktrackFrequency
// Got a signal but lost it — move back to find it again more slowly.
//
// Probes numberOfIntervals steps backward from current_freq, records
// signal levels, then tries each above-squelch candidate (sorted by
// level descending) with a full 150 ms settle + GetSignalLevelEx
// recheck using DEBOUNCE_HYSTERESIS_DB margin.  The peak-picker
// ensures the strongest candidate is tried first; if its recheck
// fails (e.g. the peak was a settle artifact from an adjacent strong
// carrier), the next-best candidate is tried as a fallback.
//
// Returns:
//   true  — *out_freq and *out_level are set to the verified values.
//   false — no candidate passed recheck; *out_freq is the last
//           probed position (for continuing the sweep).
//
bool BacktrackFrequency(freq_t current_freq, freq_t freq_interval,
                        int numberOfIntervals, freq_t freq_min,
                        freq_t freq_max,
                        freq_t *out_freq, double *out_level)
{
    double squelch = 0;
    double level = 0;
    double level_trace[numberOfIntervals];
    freq_t freq_trace[numberOfIntervals];
    bool   above_trace[numberOfIntervals];
    int i;
    int n = 2;
    int original_max = numberOfIntervals;
    int effective_max = original_max;

    //
    // Probe phase: scan backward and record levels per step
    //
    for (i = 0; i < effective_max; i++)
    {
        current_freq -= freq_interval;
        if (current_freq < freq_min)
            current_freq = freq_max - freq_interval;
        GetSquelchLevel(g_sockfd, &squelch);
        SetFreq(g_sockfd, current_freq);
        GetSignalLevelEx(g_sockfd, &level, 5, true);
        level_trace[i] = level;
        freq_trace[i]  = current_freq;
        above_trace[i] = (level >= squelch);
        if (opt_verbose)
        {
            printf("[BACKTRACK] probe[%d] freq=%s level=%.2f squelch=%.2f (%s)\n",
                   i, print_freq(current_freq), level, squelch,
                   above_trace[i] ? "above" : "below");
            fflush(stdout);
        }

        if (level >= squelch)
        {
            // Signal found — scan n=2 more steps to map the full plateau
            // so the peak-picker can centre on it rather than returning
            // the leading edge.
            if ((original_max - i) > n)
                effective_max = i + n;
        }
    }

    //
    // Collect above-squelch candidate indices
    //
    int candidate_indices[numberOfIntervals];
    int n_candidates = 0;
    for (i = 0; i < effective_max; i++)
    {
        if (above_trace[i])
        {
            candidate_indices[n_candidates++] = i;
        }
    }

    if (n_candidates == 0)
    {
        if (opt_verbose)
            printf("[BACKTRACK] no above-squelch readings found\n");
        *out_freq = current_freq;
        return false;
    }

    //
    // A single above-squelch candidate is accepted only when its level is
    // significantly above squelch (>= 5 dB margin).  This avoids locking
    // onto marginal noise spikes while allowing narrow carriers (≈10 kHz
    // bandwidth with 10 kHz probe spacing) to pass through.
    //
    #define BACKTRACK_MIN_MARGIN_DB 5.0

    if (n_candidates == 1)
    {
        int ci = candidate_indices[0];
        if (level_trace[ci] < squelch + BACKTRACK_MIN_MARGIN_DB)
        {
            if (opt_verbose)
                printf("[BACKTRACK] only 1 above-squelch candidate (%.1f dBFS, %.0f dB above squelch, need >= %.0f) — rejecting\n",
                       level_trace[ci], level_trace[ci] - squelch, BACKTRACK_MIN_MARGIN_DB);
            *out_freq = current_freq;
            return false;
        }
        if (opt_verbose)
            printf("[BACKTRACK] single above-squelch candidate at %s (%.1f dBFS, %.0f dB above squelch) — accepted\n",
                   print_freq(freq_trace[ci]), level_trace[ci], level_trace[ci] - squelch);
    }

    //
    // Sort candidate indices by level descending (insertion sort)
    //
    for (i = 1; i < n_candidates; i++)
    {
        int key = candidate_indices[i];
        double key_level = level_trace[key];
        int j = i - 1;
        while (j >= 0 && level_trace[candidate_indices[j]] < key_level)
        {
            candidate_indices[j + 1] = candidate_indices[j];
            j--;
        }
        candidate_indices[j + 1] = key;
    }

    if (opt_verbose)
    {
        printf("[BACKTRACK] peak at %s level=%.2f (%d above-squelch candidate(s))\n",
               print_freq(freq_trace[candidate_indices[0]]),
               level_trace[candidate_indices[0]],
               n_candidates);
        fflush(stdout);
    }

    // Return the best above-squelch candidate from the probe phase.
    // AdjustFrequency and the Debounce path in the caller handle
    // fine-tuning and signal persistence verification.
    int ci = candidate_indices[0];
    *out_freq = freq_trace[ci];
    *out_level = level_trace[ci];
    return true;
}
// AdjustFrequency
// Fine tuning to reach max level
// Perform a sweep between -15+15Khz around current_freq with 5kHz steps
// Return the found frequency
//
//
freq_t AdjustFrequency(freq_t current_freq, freq_t freq_interval)
{
    freq_t freq_min   = current_freq - freq_interval;
    freq_t freq_max   = current_freq + freq_interval;
    freq_t freq_steps = freq_interval;
    // FIX: +1 to include the last frequency
    long max_levels = (freq_max - freq_min) / freq_steps + 1;
    typedef struct { double level; freq_t freq; } LEVELS;
    LEVELS levels[max_levels];
    int l = 0;
    double squelch = 0;

    GetSquelchLevel(g_sockfd, &squelch);

    double level = 0;
    // FIX: use <= to include freq_max
    for (current_freq = freq_min; current_freq <= freq_max; current_freq += freq_steps)
    {
        SetFreq(g_sockfd, current_freq);
        GetSignalLevelEx(g_sockfd, &level, 5, true);
        levels[l].level = level;
        levels[l].freq  = current_freq;
        l++;
    }

    // Original first‑pass peak detection (first maximum)
    double current_level = levels[0].level;
    int start = 0, end = max_levels - 1;  // note: last index is max_levels-1
    for (l = 0; l < max_levels; l++)
    {
        if (levels[l].level >= current_level)
        {
            current_level = levels[l].level;
            start = end = l;
        }
    }
    l = start + (end - start) / 2;
    current_freq = levels[l].freq;
    SetFreq(g_sockfd, current_freq);
    double reference_level;
    GetSignalLevelEx(g_sockfd, &reference_level, 5, true);

    // Saved frequency check (unchanged)
    freq_t tolerance = 7000;
    bool found = false;
    for (int i = 0; i < SavedFreq_Max; i++)
    {
        if (current_freq >= (SavedFrequencies[i].freq - tolerance) &&
            current_freq < (SavedFrequencies[i].freq + tolerance))
        {
            if (SavedFrequencies[i].count > 4)
            {
                current_freq = SavedFrequencies[i].freq;
                found = true;
                break;
            }
        }
    }

    if (found)
    {
        SetFreq(g_sockfd, current_freq);
        GetSignalLevelEx(g_sockfd, &reference_level, 5, true);
        return levels[l].freq;   // keep your original behaviour
    }

    // Second pass – fine tuning ±5 kHz with 1 kHz steps
    freq_t reference_freq = current_freq;
    freq_min = current_freq - 5000;
    freq_max = current_freq + 5000;
    freq_steps = 1000;
    // FIX: +1 to include the last frequency
    max_levels = (freq_max - freq_min) / freq_steps + 1;
    LEVELS levels2[max_levels];
    l = 0;

    // Upper half
    for (current_freq = reference_freq + freq_steps; current_freq <= freq_max; current_freq += freq_steps)
    {
        SetFreq(g_sockfd, current_freq);
        GetSignalLevelEx(g_sockfd, &level, 5, true);
        if (level < reference_level)
            break;   // keep your original early exit
        levels2[l].level = level;
        levels2[l].freq  = current_freq;
        l++;
    }
    // Lower half
    for (current_freq = reference_freq - freq_steps; current_freq >= freq_min; current_freq -= freq_steps)
    {
        SetFreq(g_sockfd, current_freq);
        GetSignalLevelEx(g_sockfd, &level, 5, true);
        if (level < reference_level)
            break;
        levels2[l].level = level;
        levels2[l].freq  = current_freq;
        l++;
    }

    if (l == 0)
    {
        SetFreq(g_sockfd, reference_freq);
        return reference_freq;
    }

    // CORRECTED MAX SEARCH for second pass
    double max_level_val = -1e9;
    int max_idx = 0;
    for (int i = 0; i < l; i++)
    {
        if (levels2[i].level > max_level_val)
        {
            max_level_val = levels2[i].level;
            max_idx = i;
        }
    }
    current_freq = levels2[max_idx].freq;
    SetFreq(g_sockfd, current_freq);
    return current_freq;
}

// Reconnect
// Close the dead socket and open a new connection to Gqrx.
// Returns true on success, false on failure (error already printed).
//
bool Reconnect(void)
{
    int new_sockfd;
    struct sockaddr_in serveraddr;
    struct hostent *server;

    close(g_sockfd);

    new_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (new_sockfd < 0)
    {
        fprintf(stderr, "Error: Reconnect: could not create socket: %s\n",
                strerror(errno));
        return false;
    }

    server = gethostbyname(opt_hostname);
    if (server == NULL)
    {
        fprintf(stderr, "Error: Reconnect: no such host as %s\n", opt_hostname);
        close(new_sockfd);
        return false;
    }

    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    memcpy(&serveraddr.sin_addr.s_addr, server->h_addr_list[0],
           server->h_length);
    serveraddr.sin_port = htons(opt_port);

    if (connect(new_sockfd, (const struct sockaddr *)&serveraddr,
                sizeof(serveraddr)) < 0)
    {
        fprintf(stderr, "Error: Reconnect: could not connect to %s:%d: %s\n",
                opt_hostname, opt_port, strerror(errno));
        close(new_sockfd);
        return false;
    }

#ifndef TESTING_BUILD
    struct timeval tv = { .tv_sec = 3, .tv_usec = 0 };
    setsockopt(new_sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(new_sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

    g_sockfd = new_sockfd;
    g_socket_dead = false;

    fprintf(stderr, "Reconnected to Gqrx on %s:%d\n", opt_hostname, opt_port);
    return true;
}

bool ScanFrequenciesInRange(freq_t freq_min, freq_t freq_max, freq_t freq_interval)
{
    freq_t freq = 0;
    GetCurrentFreq(g_sockfd, &freq);
    double level = 0;
    GetSignalLevel(g_sockfd, &level );
    double squelch = 0;
    GetSquelchLevel(g_sockfd, &squelch);

    size_t freqeuencies_count = ((opt_max_freq-opt_min_freq)/opt_scan_bw); //for loop boundary

    freq_t current_freq = freq_min;

    SetFreq(g_sockfd, freq_min);
    int saved_idx = 0, current_saved_idx = 0;
    int sweep_count = 0;
    freq_t last_freq;
    bool saved_cycle = false;
    // minimum hit threshold on frequency already seen (count): above this the freq is a candidate
    int min_hit_threshold = 2;
    // maximum miss threshold on frequencies not seen anymore: above this the candidate count is decremented
    // note: during monitoring on active freq the counter can reach high values so this threshold should not be too high
    // otherwise the chance to exclude the freq is very low and will continue to monitor even after prolonged inactive time
    int max_miss_threshold = 20;
    long sleep_cyle         = 10000 ; // wait 10ms after setting freq to get signal level
    long sleep_cyle_saved   = 85000 ; // skipping freqeuency need more time to get signal level
    long sleep_cycle_active = 500000; // skipping from active frequency need more time to wait squelch level to kick in
    bool skip = false;   // user input
    char timestamp[BUFSIZE] = {0};
    int  success_counter    = 0;  // number of correctly acquired signals, reset on bad signals or reaching success_factor
    int  success_factor     = 5; // improving sleep cycle every success_factor of times

    while (true)
    {
#ifdef TESTING_BUILD
        if (g_testing_max_full_sweeps >= 0 &&
            g_testing_sweep_full_count >= g_testing_max_full_sweeps)
            break;
#endif
        for ( size_t i = 0 ; i < freqeuencies_count; i++)
        {
            CheckUserInput();

            if (g_socket_dead)
                Reconnect();
            IsBannedFreq(&current_freq); // test and change current_frequency to next available slot;
            SetFreq(g_sockfd, current_freq);
#ifndef OSX
            if (opt_vox) VoxAudioReset();
#endif
            if (saved_cycle)
                usleep((skip)?sleep_cycle_active:sleep_cyle_saved);
            else
                usleep((skip)?sleep_cycle_active:sleep_cyle);

            GetSquelchLevel(g_sockfd, &squelch);
            GetSignalLevelEx(g_sockfd, &level, 5, false);

            if (opt_verbose)
            {
                printf("\nFreq: %s Signal: %2.2f Squelch: %2.2f\n", print_freq(current_freq), level, squelch);
                fflush (stdout);
            }

            if (level >= squelch)
            {
                // we have a possible match, but sometimes level oscillates after a squelch miss
                bool still_good = Debounce(current_freq, level);
                if (!still_good)
                {
                    // Signal lost
                    // it could be a ghosts signal because we are running too fast, slow down a bit
                    success_counter = 0; // stop incrementing sleep cycle for a while
                    sleep_cyle+= 5000;   // add penality
                    if (sleep_cyle > 50000)
                        sleep_cyle = 50000;
                    if (opt_verbose)
                    {
                        printf("Missing signal. Slowing down: %ld ms wait time.\n", sleep_cyle/1000);
                        fflush (stdout);
                    }
                    // tries to recover to get back the signal, check our steps...
                    //
                    // Always attempt to backtrack and find the source of the
                    // signal, regardless of saved-frequency proximity.  The
                    // full probe ensures we don't miss new or shifted signals.
                    {
                        freq_t backtrack_freq;
                        double bt_level;
                        if (BacktrackFrequency(current_freq, freq_interval, 4,
                                               freq_min, freq_max,
                                               &backtrack_freq, &bt_level))
                        {
                            current_freq = backtrack_freq;
                            level       = bt_level;
                            goto carrier_acquired;
                        }
                        else
                        {
                            if (opt_verbose)
                            {
                                printf("[BACKTRACK] all candidates rejected — continuing sweep\n");
                                fflush(stdout);
                            }
                            current_freq = backtrack_freq;
                            if (IsBannedFreq(&current_freq))
                                skip = true;
                        }
                    }
                    continue;
                }
                else
                {
                    // Frequency acquired successfully
                    // Or.. we could have jumped on another frequency with a valid signal nearby.
                    success_counter++;
                    if (success_counter > success_factor)
                    {
                        // Increase speed a little bit in order to compensates signal lost because of bad luck
                        // while we moved the frequency (signal disappearing)
                        sleep_cyle-= 1000; // add a reward
                        if (sleep_cyle < 10000)
                            sleep_cyle = 10000;
                        if (opt_verbose)
                        {
                            printf("Signals acquired successfully. Speeding up: %ld ms wait time.\n", sleep_cyle/1000);
                            fflush (stdout);
                        }
                        success_counter = 0; // stop decrementing sleep cycle for a while
                    }
                }

                // Label for both normal acquisition and backtrack acquisition
                carrier_acquired:

                current_freq = AdjustFrequency(current_freq, freq_interval/2);
                if (IsBannedFreq(&current_freq))
                {
                    skip = true;
                }
                else
                {
                    SaveFreq(current_freq);
                    if (opt_record)
                    {
                        StartRecording(g_sockfd);
                    }

                    struct timeval hit_time = GetTime(timestamp);
                    printf ("[%s] Freq: %s active, Level: %2.2f/%2.2f ",
                            timestamp, print_freq(current_freq),
                            level, squelch );
                    fflush(stdout);
                    // Wait user input or delay time after signal lost
                    skip = WaitUserInputOrDelay(opt_delay, &current_freq);
                    DiffTime(timestamp, &hit_time);
                    if (opt_record)
                    {
                        StopRecording(g_sockfd);
                    }
                    printf (" [elapsed time %s]\n", timestamp);
                    fflush(stdout);
                }
                if (skip)
                {
                    sweep_count = 0; // reactivate sweep scan
                    continue; // go to the next freq set in current_freq
                }
            }
            else
            {
                skip = false;
                // no activities
                if (saved_cycle)
                {
                    // miss count on already seen frequency
                    if (++SavedFrequencies[current_saved_idx].miss > max_miss_threshold)
                    {
                        SavedFrequencies[current_saved_idx].count--;
                        SavedFrequencies[current_saved_idx].miss = 0;
                    }
                }
            }

            // Loop saved freq after a while
            if (sweep_count > 40)
            {
                if (!saved_cycle)
                {
                    // start cycling on saved frequencies
                    last_freq = current_freq;
                    saved_cycle = true;
                }
                // search candidates into saved frequencies
                while ( (SavedFrequencies[saved_idx].count < min_hit_threshold) && //hit threshold
                        (saved_idx < SavedFreq_Max) )
                {
                    saved_idx++;
                }
                if (saved_idx >= SavedFreq_Max)
                {
                    saved_idx = 0;
                    sweep_count = 0; // reactivates sweep scan
                    saved_cycle = false;
                    current_freq = last_freq;
                    current_freq = ceil( current_freq / (double)opt_scan_bw ) * opt_scan_bw;
                }
                else // found one
                {
                    freq_t candidate = SavedFrequencies[saved_idx].freq;
                    // Validate the frequency: must be within current sweep range and not zero
                    if (candidate >= freq_min && candidate <= freq_max && candidate != 0)
                    {
                        current_freq = candidate;
                        current_saved_idx = saved_idx;
                        saved_idx++;
                        if (saved_idx >= SavedFreq_Max)
                            saved_idx = 0;
                        continue;
                    }
                    else
                    {
                        // Corrupt entry – reset it and skip
                        SavedFrequencies[saved_idx].freq = 0;
                        SavedFrequencies[saved_idx].count = 0;
                        SavedFrequencies[saved_idx].miss = 0;
                        saved_idx++;
                        if (saved_idx >= SavedFreq_Max)
                            saved_idx = 0;
                        continue;
                    }
                }
            }
            current_freq+=freq_interval;
            if (current_freq > freq_max)
                current_freq = freq_min;
            sweep_count++;
        }
#ifdef TESTING_BUILD
        g_testing_sweep_full_count++;
#endif
    }
    return true;
}

void SetOptDefaults(void)
{
    memset(SavedFrequencies, 0, sizeof(SavedFrequencies));
    SavedFreq_Max = 0;
    memset(BannedFrequencies, 0, sizeof(BannedFrequencies));
    BannedFreq_Max = 0;
    opt_scan_bw  = 10000;
    opt_delay    = 1;
    opt_max_listen = 100000;
    opt_speed    = 1;
    opt_date     = 0;
    opt_record   = false;
    opt_verbose  = false;
#ifndef OSX
    opt_vox      = false;
    opt_max_probe = 0;
#endif
#ifdef TESTING_BUILD
    g_testing_sweep_full_count = 0;
    g_testing_max_full_sweeps = -1;
    g_testing_bookmark_loop_count = 0;
    g_testing_max_bookmark_loops = -1;
#endif
}

void FreeFrequencies(void)
{
    if (!Frequencies) return;
    for (int i = 0; i < Frequencies_Max; i++)
        for (int k = 0; k < Frequencies[i].tag_max; k++)
            free(Frequencies[i].tags[k]);
    free(Frequencies);
    Frequencies = NULL;
    Frequencies_Max = 0;
}

void ResetOptTags(void)
{
    for (int i = 0; i < opt_tag_max; i++) {
        free(opt_tags[i]);
        opt_tags[i] = NULL;
    }
    opt_tag_max = 0;
}

#ifndef TESTING_BUILD
int main(int argc, char **argv) {
    int sockfd, portno, n;
    char *hostname;
    char buf[BUFSIZE];
    FILE *bookmarksfd = NULL;

    opt_hostname = (char *) g_hostname;
    opt_port     = g_portno;
    opt_delay    = g_delay;
    ParseInputOptions(argc, argv);
    has_range = (opt_min_freq != 0 || opt_max_freq != 0);

#ifndef OSX
    if (opt_vox)
    {
        if (opt_max_listen == 0)
        {
            fprintf(stderr, "Error: --vox requires -l/--max-listen.\n"
                    "       -l sets the silence timeout: how long to wait after\n"
                    "       someone stops talking before moving to the next\n"
                    "       frequency.\n"
                    "       Example: -l 3000 for 3 seconds of silence.\n");
            print_usage(argv[0]);
        }
        if (!VoxAudioInit())
        {
            fprintf(stderr, "[ WARNING ] VOX disabled. "
                    "Run 'gqrx-scan-setup-audio.sh attach' to enable audio capture.\n");
            opt_vox = false;
        }
    }
#endif

    // post validating
    if (opt_tag_search && (opt_scan_mode == sweep || opt_scan_mode == fft) )
    {
        // Not supported yet
        printf ("Error: Optional tag based search is not supported in sweep or fft mode.\n");
        printf ("       Please specify '-m bookmark' mode.\n");
        print_usage(argv[0]);
    }

    char from[256], to[256];

    if (
        (opt_min_freq > opt_max_freq)             || // bad range or only min specified
        (opt_min_freq == 0 && opt_max_freq > 0)   || // or  only max specified
        ((opt_min_freq != 0 && opt_max_freq != 0) && // or they are equal but different from 0
         (opt_min_freq == opt_max_freq)                  )
       ) // or only max specified
    {
        strcpy (from, print_freq(opt_min_freq));
        strcpy (to,   print_freq(opt_max_freq));
        printf ("Error: Invalid frequency range: begin:%s, end=%s.\n", from, to);
        printf ("       Please specify '-f <freq>' or '-b <begin_freq> -e <end_freq>.\n");
        print_usage(argv[0]);
    }


    // here min & max could be equal to 0 because the user specified -f flag
    sockfd = Connect(opt_hostname, opt_port);
    g_sockfd = sockfd;

    // Read the channel filter bandwidth for the step/merge default.
    freq_t filter_bw = 10000;
    GetFilterBandwidth(g_sockfd, &filter_bw);

    // If the user did not specify -s, default the step/merge distance to
    // the Gqrx channel filter bandwidth instead of the hardcoded 10 kHz.
    if (opt_scan_bw == g_default_scan_bw && filter_bw >= 100)
        opt_scan_bw = filter_bw;

    g_gqrx_supports_fft = CheckFFTSupport();
    
    if (opt_min_freq == 0 && opt_max_freq == 0)
    {
        if (g_gqrx_supports_fft)
        {
            freq_t fft_center;
            GetSafeRange(&opt_min_freq, &opt_max_freq, &fft_center);
        }
        else if (opt_scan_mode != fft)
        {
            freq_t current_freq;
            GetCurrentFreq(g_sockfd, &current_freq);
            opt_min_freq = current_freq > g_freq_delta
                         ? current_freq - g_freq_delta : 0;
            opt_max_freq = current_freq + g_freq_delta;
        }
        else 
        {
            printf ("Error: Gqrx does not support FFT mode (yet).\n");
            print_usage(argv[0]);
        }
    }
    else if (opt_tag_search && opt_min_freq == opt_max_freq)
    {
        /* Tag search without a range — warning only. */
        printf ("Warning: search tags on the entire frequency range!\n");
    }

    strcpy (from, print_freq(opt_min_freq));
    strcpy (to,   print_freq(opt_max_freq));
    printf ("Frequency range set from %s to %s.\n", from, to);

    if (opt_scan_mode == bookmark)
    {
        Frequencies = malloc(FREQ_MAX * sizeof(FREQ));
        bookmarksfd = Open(g_bookmarksfile);
        LoadFrequencies (bookmarksfd);
    }

    if (opt_tag_search)
    {
        char str [1024];
        printf ("Tags to search: ");
        for (int i = 0; i < opt_tag_max ; i++)
        {
            printf ("[%s] ", opt_tags[i] );
        }
        printf ("\n");

        // Check if there are any
        int count = 0;
        for (int i = 0 ; i < Frequencies_Max; i++ )
        {
            if (FilterFrequency(i) != (freq_t) 0 )
                count++;
        }
        if (count == 0)
        {
            printf("No match. Exit.\n");
            exit (1);
        }
        printf ("%d candidate frequencies found.\n", count);
    }

    if (opt_scan_mode == sweep)
    {
        ScanFrequenciesInRange(opt_min_freq, opt_max_freq, opt_scan_bw);
    }
    else if (opt_scan_mode == fft)
    {
        ScanFrequenciesInRangeFFT(opt_min_freq, opt_max_freq);
    }
    else
    {
        ScanBookmarkedFrequenciesInRange(opt_min_freq, opt_max_freq);
    }

    if (bookmarksfd) fclose(bookmarksfd);
    close(g_sockfd);
    FreeFrequencies();
#ifndef OSX
    VoxAudioShutdown();
#endif
    return 0;
}
#endif /* TESTING_BUILD */
