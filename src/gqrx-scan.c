/*
MIT License

Copyright (c) 2025 neural75

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

static char freq_string[BUFSIZE] = {0};


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
    printf ("%s\n\t\t[-h|--host <host>] [-p|--port <port>] [-m|--mode <sweep|bookmark>]\n", name);
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
    printf ("                               Possible values for <mode>: sweep, bookmark\n");
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
char * print_freq (freq_t freq)
{
    // fist round up to khz
    freq = round ( freq / 1000.0 ) * 1000.0;
    long Ghz = freq/1000000000;
    long Mhz = (freq/1000000)%1000;
    long Khz = (freq/1000)%1000;

    freq_string[0] = '\0';
    char temp[256];
    if (Ghz)
    {
        sprintf (temp, "%ld.%3.3ld.%3.3ld GHz", Ghz, Mhz, Khz);
        strcat(freq_string, temp);
        return freq_string;
    }
    if (Mhz)
    {
        sprintf (temp, "%ld.%3.3ld MHz", Mhz, Khz);
        strcat(freq_string, temp);
        return freq_string;
    }

    sprintf (temp, "%ld KHz", Khz);
    strcat(freq_string, temp);
    return freq_string;
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
time_t GetTime(char *timestamp)
{
    time_t etime = time(NULL);
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
    return etime;
}

// Calculate difference in time in [dd days][hh:][mm:][ss secs]
time_t DiffTime(char *timestamp, time_t start_time)
{
    double seconds;
    time_t etime = time (NULL);
    seconds = difftime(etime , start_time);

    // casting to time_t, someone with better idea may change this to be more consistent
    time_t elapsed = (time_t)seconds;
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
        char sec[16];
        sprintf(sec, "%2.2d sec", ltime->tm_sec);
        strcat(timestamp, sec);
    return elapsed;
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
// WaitUserInputOrDelay
// Waits for user input or a delay after the carrier is gone
// Returns if the user has pressed <space> or <enter> to skip frequency
//
bool WaitUserInputOrDelay (long delay, freq_t *current_freq)
{
    double    squelch;
    double  level;
    long    sleep_time = 0, listen_time = 0, consecutive_silent = 0, sleep = 100000; // 100 ms
    long    vox_sample_time = 10000;  // 10 ms VOX audio capture window (µs)
    int     exit = 0;
    char    c;
    bool    skip = false;
    bool    pause = false;
    bool    voice_was_detected = false;
    int     socket_failures = 0;

#ifndef OSX
    __fpurge(stdin);
#else
    fpurge(stdin);
#endif
    nonblock(NB_ENABLE);

#ifndef OSX
    // Flush stale audio left in the pipe from the previous frequency,
    // so VoxAudioHasSignal() only measures this frequency's audio.
    if (opt_vox)
        VoxAudioFlush();
#endif

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
            usleep(sleep);
            continue;
        }
        socket_failures = 0;
        exit = kbhit();
        if (exit !=  0)
        {
            c = fgetc(stdin);
            switch (c)
            {
                case ' ':
                case '\n':
                {
                    exit = 1; // exit
                    skip = true;
                    break;
                }
                case 'b':
                {
                    // Ban a frequency
                    BanFreq(*current_freq);
                    exit = 1;
                    skip = true;
                    break;
                }
                case 'c':
                {
                    // Clear all bans
                    ClearAllBans();
                    exit = 0;
                    break;
                }
                case 'p':
                {
                    // pause until another 'p'
                    pause ^= true; // switch pause mode
                    exit = 0;
                    break;
                }
                default:
                    exit = 0;

            }
            if (exit == 1)
                break;
        }

        if (pause)
        {
            usleep (sleep);
            continue;
        }

        listen_time += sleep;

#ifndef OSX
        // Voice-activity detection — when --vox is active and the carrier
        // is present, poll for fresh audio with a vox_sample_time window.
        // If voice is detected we reset both the listen_time and the
        // consecutive-silence counter, so the frequency stays active.
        // On silence we only increment the consecutive-silence counter.
        if (opt_vox && level >= squelch)
        {
            if (VoxAudioHasSignal(vox_sample_time))
            {
                voice_was_detected = true;
                listen_time = 0;
                consecutive_silent = 0;
                sleep_time = 0;
            }
            else
            {
                consecutive_silent++;
                if (!VoxAudioIsAlive())
                {
                    // One-shot restart of the pw-cat pipe.  If this is the
                    // first time the pipe has died, try to respawn pw-cat.
                    // If that fails (or a restart was already attempted),
                    // disable VOX permanently for this scan.
                    static bool vox_restart_attempted = false;
                    if (vox_restart_attempted || !VoxAudioRestart())
                    {
                        fprintf(stderr, "[ WARNING ] Audio capture pipe closed. "
                                "Disabling VOX.\n");
                        opt_vox = false;
                    }
                    vox_restart_attempted = true;
                }
            }
        }
#endif

        // Two-phase VOX timing:
        //   Phase 1 (probe)  — no voice heard yet → exit after probe_time
        //   Phase 2 (active) — voice seen → exit after hangup_time of silence
#ifndef OSX
        if (opt_vox && level >= squelch)
        {
            long threshold = voice_was_detected ? opt_max_listen : opt_max_probe;
            long limit = voice_was_detected ? consecutive_silent * sleep : listen_time;
            if (threshold > 0 && limit >= threshold)
            {
                exit = 1;
                skip = true;
            }
        }
        else
#endif
          if (opt_max_listen != 0)
          {
              // Non-VOX path: original listen-time cap
              if (opt_max_listen <= listen_time)
              {
                  exit = 1;
                  skip = true;
              }
          }

        // exit = 0
        if (level < squelch )
        {


            // Signal drop below the threshold, start counting sleep time
            sleep_time += sleep;
            if (sleep_time > delay)
            {

                exit = 1;
                skip = false;
            }
        }
        else
        {
            sleep_time = 0; //
        }
        // someone is tx'ing
        usleep(sleep);
    } while ( !exit ) ;

    nonblock(NB_DISABLE);


    // restart scanning
    *current_freq+=g_ban_tollerance;
    // round up to next near tenth of khz  145892125 -> 145900000
    *current_freq = ceil( *current_freq / (double)opt_scan_bw ) * opt_scan_bw;

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
                        time_t hit_time = GetTime(timestamp);
                        printf ("[%s] Freq: %s active [%s], Level: %2.2f/%2.2f ",
                                timestamp, print_freq(current_freq),
                                Frequencies[i].descr, level, squelch);
                        fflush(stdout);
                        skip = WaitUserInputOrDelay(opt_delay, &current_freq);
                        time_t elapsed = DiffTime(timestamp, hit_time);
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

                    time_t hit_time = GetTime(timestamp);
                    printf ("[%s] Freq: %s active, Level: %2.2f/%2.2f ",
                            timestamp, print_freq(current_freq),
                            level, squelch );
                    fflush(stdout);
                    // Wait user input or delay time after signal lost
                    skip = WaitUserInputOrDelay(opt_delay, &current_freq);
                    time_t elapsed = DiffTime(timestamp, hit_time);
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
    if (opt_tag_search && (opt_scan_mode == sweep) )
    {
        // Not supported yet
        printf ("Error: Optional tag based search is not supported in sweep mode.\n");
        printf ("       Please specify '-m bookmark' mode.\n");
        print_usage(argv[0]);
    }

    char from[256], to[256];

    if (
        (opt_min_freq > opt_max_freq)                        || // bad range or only min specified
        (opt_min_freq == 0 && opt_max_freq > 0)              || // or  only max specified
        ((opt_min_freq != 0 && opt_max_freq != 0) &&            // or they are equal but different from 0
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

    if (!opt_tag_search) // sweep or bookmark
    {
        if (opt_min_freq == 0 && opt_max_freq == 0)
        {
            freq_t current_freq;
            GetCurrentFreq(g_sockfd, &current_freq);
            opt_min_freq = current_freq - g_freq_delta;
            opt_max_freq = current_freq + g_freq_delta;
        }
    }
    else
    {
        // more tollerating with tags (bookmark mode)
        if (opt_min_freq == opt_max_freq) // user has not set values or has set equals.
        {
            printf ("Warning: search tags on the entire frequency range!\n");
        }

    }

    if (opt_scan_mode == bookmark)
    {
        Frequencies = malloc(FREQ_MAX * sizeof(FREQ));
    }

    strcpy (from, print_freq(opt_min_freq));
    strcpy (to,   print_freq(opt_max_freq));
    printf ("Frequency range set from %s to %s.\n", from, to);

    if (opt_scan_mode == bookmark)
    {
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
