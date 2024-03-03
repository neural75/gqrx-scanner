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


/*
 * gqrx-scanner
 * A simple frequency scanner for gqrx
 *
 * usage: gqrx-scanner [-h|--host <host>] [-p|--port <port>] [-m|--mode <sweep|bookmark>]
 *                  [-f <central frequency>] [-b|--min <from freq>] [-e|--max <to freq>]
 *                  [-d|--delay <lingering time on signals>]
 *                  [-t|--tags <"tag1|tag2|...">]
 *                  [-x|--disable-sweep-store] [-s <min_hit_to_remember>] [-m <max_miss_to_forget>]
 */
#define _GNU_SOURCE // strcasestr
#include <stdio.h>
#ifndef OSX
#include <stdio_ext.h>
#else
#endif
#include <stdlib.h>
#include <pthread.h>
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
#include "gqrx-prot.h"

#define NB_ENABLE    true
#define NB_DISABLE   false
#define PREV_FREQ_MAX 6

//
// Globals definitions
//
typedef struct {
    freq_t freq; // frequency in Mhz
    double noise_floor; // averages noise floor of frequency
    double squelch_delta_top; // calculated squelch delta top
    int count; // hit count on sweep scan
    int miss;  // miss count on sweep scan
    char descr[BUFSIZE]; // ugly buffer for descriptions: TODO convert it in a pointer
    char *tags[TAG_MAX]; // tags
    int   tag_max;
}FREQ;

typedef enum
{
    sweep,
    bookmark
} SCAN_MODE;

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

static char freq_string[BUFSIZE] = {0};


//
// Defaults
//
const char     *g_hostname          = "localhost";
const int       g_portno            = 7356;
const int       g_udp_listen_portno = 7355;
const freq_t    g_freq_delta        = 1000000; // +- 1Mhz default bandwidth to scan from tuned freq.
const freq_t    g_default_scan_bw   = 10000;   // default scan frequency steps (10Khz)
const freq_t    g_ban_tollerance    = 10000;   // +- 10Khz bandwidth to ban from current freq.
const long      g_delay             = 2500000; // 2 sec //LWVMOBILE: Doesn't this default value equal 20 seconds? Changing to a lower number. Was 2000000000
//LWVMOBILE: Above variable was set WAY too high, it wasn't 2 seconds, but 2000 seconds.
const char     *g_bookmarksfile     = "~/.config/gqrx/bookmarks.csv";
//LWVMOBILE: Insert new constants here for scan speed and date format?? Or just use variable with speed set already

//
// Input options
//
char           *opt_hostname = NULL;
int             opt_port = 0;
int             opt_udp_listen_port = 0;
freq_t          opt_freq = 0;
freq_t          opt_min_freq = 0;
freq_t          opt_max_freq = 0;
freq_t          opt_scan_bw = g_default_scan_bw;
long            opt_delay = 0; //LWVMOBILE: Changing this variable from 0 to 250 attempt to fix 'no delay argument given' stoppage on bookmark scan
//LWVMOBILE: New variables inserted here
long            opt_speed = 250000;
long            opt_date = 0;
//LWVMOBILE; End new variables.
SCAN_MODE       opt_scan_mode = sweep;
bool            opt_tag_search = false;
bool            opt_udp_listen = false;
char           *opt_tags[TAG_MAX] = {0};
int             opt_tag_max = 0;
bool            opt_disable_store = false;
long            opt_max_listen = 0;
// only for debug
bool            opt_verbose = false;

// set squelch delta
double          opt_squelch_delta_bottom = 0.0;
double          opt_squelch_delta_top = 0.0;
bool            opt_squelch_delta_auto_enable = false;
//
// Local Prototypes
//
bool BanFreq (freq_t freq_current);
bool IsBannedFreq (freq_t *freq_current);
void ClearAllBans ( void );

//
// ParseInputOptions
//
void print_usage ( char *name )
{

    printf ("Usage:\n");
    printf ("%s\n\t\t[-h|--host <host>] [-p|--port <port>] [-m|--mode <sweep|bookmark>]\n", name);
    printf ("\t\t[-f <central frequency>] [-b|--min <from freq>] [-e|--max <to freq>]\n");
    printf ("\t\t[-d|--delay <lingering time in milliseconds>]\n");
    printf ("\t\t[-l|--max-listen <maximum listening time in milliseconds>]\n");
    printf ("\t\t[-t|--tags <\"tag1|tag2|...\">]\n");
    printf ("\t\t[-v|--verbose]\n");
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
    printf ("-d, --delay <time>           Lingering time in milliseconds before the scanner reactivates. Default 2000\n");
    printf ("-l, --max-listen <time>      Maximum time to listen to an active frequency. Default 0, no maximum\n");
    printf ("-x, --speed <time>           Time in milliseconds for bookmark scan speed. Default 250 milliseconds.\n");
    printf ("                               If scan lands on wrong bookmark during search, use -x 500 (ms) to slow down speed\n");
    printf ("-y  --date                   Date Format, default is 0.\n");
    printf ("                               0 = mm-dd-yy\n");
    printf ("                               1 = dd-mm-yy\n");
    printf ("-q, --squelch_delta <dB>|a<dB> If set creates bottom squelch just for listening.\n");
    printf ("                             It may reduce unnecessary squelch audio supress.\n");
    printf ("                             Default: 0.0\n");
    printf ("                             Ex.: 6.5\n");
    printf ("                             Place \"a\" switch before <dB> value to turn into auto mode\n");
    printf ("                             It will determine squelch delta based on noise floor and\n");
    printf ("                             <dB> value will determine how far squelch delta will be placed from it.\n");
    printf ("                             Ex.: a0.5\n");
    printf ("-a, --squelch_delta_top <dB> It maps squelch levels for an each scanned frequency\n");
    printf ("                             based on noise floor + provided value.\n");
    printf ("-u, --udp_listen             Experimental: Trigger listening on UDP audio signal.\n");
    printf ("                             Make sure that UDP button is pushed.\n");
    printf ("-t, --tags <\"tags\">        Filter signals. Match only on frequencies marked with a tag found in \"tags\"\n");
    printf ("                             \"tags\" is a quoted string with a '|' list separator: Ex: \"Tag1|Tag2\"\n");
    printf ("                             tags are case insensitive and match also for partial string contained in a tag\n");
    printf ("                             Works only with -m bookmark scan mode\n");
    //printf ("\t\t[-x|--disable-sweep-store] [-s <min hit>] [-m <max miss>]
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
                {"verbose",               no_argument,       0, 'v'},
                {"help",                  no_argument,       0, 'w'},
                {"host",            required_argument, 0, 'h'},
                {"port",            required_argument, 0, 'p'},
                {"mode",            required_argument, 0, 'm'},
                {"freq",    required_argument, 0, 'f'},
                {"min",     required_argument, 0, 'b'},
                {"max",     required_argument, 0, 'e'},
                {"step",    required_argument, 0, 's'},
                {"tags",    required_argument, 0, 't'},
                {"delay",   required_argument, 0, 'd'},
                {"speed",   required_argument, 0, 'x'},
                {"date",    required_argument, 0, 'y'},
                {"squelch_delta",    required_argument, 0, 'q'},
                {"squelch_delta_top",    required_argument, 0, 'a'},
                {"udp_listen",    no_argument, 0, 'u'},
                {"max-listen",       required_argument, 0, 'l'},
                {0, 0, 0, 0}
            };
        /* getopt_long stores the option index here. */
        int option_index = 0;

        c = getopt_long (argc, argv, "vwh:p:m:f:b:e:s:t:d:x:y:q:a:l:u:",
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
            case 'v':
                opt_verbose = true;
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

                if ((opt_max_listen = atol(optarg)) == 0)
                {
                    printf ("Error: -%c: Invalid time\n", c);
                    print_usage(argv[0]);
                }
                opt_max_listen *= 1000; // in microsec
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

            case 'q':
                if (optarg[0] == '-')
                {
                    printf ("Error: -%c: option requires an argument\n", c);
                    print_usage(argv[0]);
                }
                if (optarg[0] == 'a')
                {
                    if ((opt_squelch_delta_bottom = atof(optarg+1)) == 0)
                    {
                        printf ("Error: -%c: Invalid squelch level\n", c);
                        print_usage(argv[0]);
                    }
                    opt_squelch_delta_auto_enable = true;
                }
                else {
                    if ((opt_squelch_delta_bottom = atof(optarg)) == 0)
                    {
                        printf ("Error: -%c: Invalid squelch level\n", c);
                        print_usage(argv[0]);
                    }
                }
                printf("Squelch delta set: %f\n", opt_squelch_delta_bottom);
                break;

            case 'a':
                if (optarg[0] == '-')
                {
                    printf ("Error: -%c: option requires an argument\n", c);
                    print_usage(argv[0]);
                }
                if ((opt_squelch_delta_top = atof(optarg)) == 0)
                {
                    printf ("Error: -%c: invalid squelch level\n", c);
                    print_usage(argv[0]);
                }
                printf("Squelch delta top set: %f\n", opt_squelch_delta_top);
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
            case 'u':
                if (opt_scan_mode == sweep){
                    printf ("Cannot use --udp_listen in sweep mode.\n");
                    print_usage(argv[0]);
                }
                opt_udp_listen = true;
                break;
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

//LWVMOBILE: It is very tempting to lop off part of this function, how many TX lasts more than a few seconds or minutes??
//LWVMOBILE: Will still need to rework eventually, this will cause any time greater than 60 minutes to roll over back to 0 I believe
//neural: Why bother? if there are transmissions lasting more than 1 hour or 1 day, the string should be consistent with the long duration.
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
        char days[10];
        sprintf(days, "%2d days ", ltime->tm_mday);
        strcat(timestamp, days);
    }
    if (ltime->tm_hour > (int)(ltime->tm_gmtoff/3600))
    {
        char hours[10];
        sprintf(hours, "%2.2d:", (int)(ltime->tm_hour - (ltime->tm_gmtoff/3600)) );
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
// WaitUserInputOrDelay
// Waits for user input or a delay after the carrier is gone
// Returns if the user has pressed <space> or <enter> to skip frequency
//
bool WaitUserInputOrDelay (int sockfd, long delay, freq_t *current_freq)
{
    double    squelch;
    double  level;
    long    sleep_time = 0, listen_time = 0, sleep = 100000; // 100 ms
    int     exit = 0;
    char    c;
    bool    skip = false;
    bool    pause = false;

#ifndef OSX
    __fpurge(stdin);
#else
    fpurge(stdin);
#endif
    nonblock(NB_ENABLE);

    do
    {
        GetCurrentFreq(sockfd,  current_freq);
        GetSquelchLevel(sockfd, &squelch);
        GetSignalLevel(sockfd,  &level );
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
        if (opt_max_listen != 0 && opt_max_listen <= listen_time) {
            exit = 1;
            skip = true;
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

filefd = fopen(filename2, "r");

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
    char *line;
    bool start = false;
    char *freq, *other;
    int i = 0;

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
            char * token = strtok(line, "; "); // freq
            sscanf(token, "%llu", &Frequencies[i].freq);
            token = strtok(NULL, ";"); // descr
            strncpy(Frequencies[i].descr, token , BUFSIZE);
            token = strtok(NULL, ";"); // mode
            token = strtok(NULL, ";"); // bw
            token = strtok(NULL, ";"); // tags, comma separated

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
            //printf(":%llu: %s\n", Frequencies[i].freq, Frequencies[i].descr);
            i++;
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

// by design, the first element is the current frequency
// armed every *for* iteration
void PrevFreqIndexes(size_t * indexes , size_t append) {
    for (size_t i = PREV_FREQ_MAX - 1; i > 0 ; i--)
    {
        indexes[i] = indexes[i-1];
    }
    indexes[0] = append;
}

bool ScanBookmarkedFrequenciesInRange(int sockfd, int udp_sockfd, freq_t freq_min, freq_t freq_max, double squelch_delta)
{
    size_t indexes[PREV_FREQ_MAX] = {0};
    freq_t freq = 0;
    GetCurrentFreq(sockfd, &freq);
    double level = 0;
    GetSignalLevel(sockfd, &level );
    double squelch = 0;
    double squelch_backup = 0;
    double * chosen_squelch = NULL;
    GetSquelchLevel(sockfd, &squelch);

    if (opt_squelch_delta_top == 0.0)
    {
        chosen_squelch = &squelch;
    }

    freq_t current_freq = freq_min;

    bool skip = false;
    long sleep_cycle_active = 500000;   // skipping from active frequency need more time to wait squelch level to kick in
    long sleep_cyle_saved   = 85000 ;   // skipping freqeuency need more time to get signal level

    long slow_scan_cycle    = 1000000;   // LWVMOBILE: Just doubling numbers to slow down scan time in bookmark search, 1,000,000 = 1 second. EDIT: DOES THIS VARIABLE DO ANYTHING?
    long slow_cycle_saved   = 250000;  // LWVMOBILE: Just doubling numbers to slow down scan time in bookmark search. THIS ONE SEEMS TO ACTUALLY SLOW SCAN SPEED DOWN.
    char timestamp[BUFSIZE] = {0};

    // Noise floor calibration
    // Moving it here to again: mitigate overshooting
    // Now unfortunately it's a) slow and b) it is done only once
    // But now it's more accurate
    printf("Calibrating noise floor...");
    fflush(stdout);
    for (int j = 0 ; j < 5; j++)
    {
        for (int i = 0; i < Frequencies_Max; i++)
        {
            if ((current_freq = FilterFrequency(i)) == (freq_t) 0 )
                continue;
            if (IsBannedFreq(&current_freq))
                continue;
            if (
                (
                ( current_freq >= freq_min) &&         // in the valid range
                ( current_freq <  freq_max)
            ) || (freq_min == freq_max)  // or using the entire frequencies
            )
            {
                SetFreq(sockfd, current_freq);
                usleep(500000);
                GetSignalLevel(sockfd, &level );
                while (level >= squelch)
                {
                    GetSquelchLevel(sockfd, &squelch);
                    usleep(500000);
                    GetSignalLevel(sockfd, &level );
                }
                Frequencies[i].noise_floor = (Frequencies[i].noise_floor + level)/2;
                // that's lazy programming VV but works
                Frequencies[i].squelch_delta_top = Frequencies[i].noise_floor + opt_squelch_delta_top;
            }
        }
    }
    printf(" done.\n");

    bool udp_signal = false;

    while (true)
    {
        CheckUserInput();

        for (int i = 0; i < Frequencies_Max; i++)
        {
            //printf("\rNoise floor: %2.2f  ", Frequencies[i].noise_floor);
            //fflush(stdout);

            if ((current_freq = FilterFrequency(i)) == (freq_t) 0 )
                continue;
            if (IsBannedFreq(&current_freq))
                continue;
            if (
                (
                ( current_freq >= freq_min) &&         // in the valid range
                ( current_freq <  freq_max)
            ) || (freq_min == freq_max)  // or using the entire frequencies
            )
            {
                PrevFreqIndexes(indexes, i);
                // Found a bookmark in the range
                SetFreq(sockfd, current_freq);
                GetSquelchLevel(sockfd, &squelch);
                //usleep((skip)?sleep_cycle_active:sleep_cyle_saved);       //LWVMOBILE: Perhaps place a small sleep here of 1000ms, slow scan to prevent 'slipping' issue in bookmark search.
                //usleep((skip)?slow_scan_cycle:slow_cycle_saved);          //LWVMOBILE: Find a way to implement these variables as a command line option -s 'slow scan' and input time in milli-seconds.
                usleep((skip)?slow_scan_cycle:opt_speed);                   //LWVMOBILE: Using new variable set by default and also by user switch. Seems to work. GJ ME.
                // LWVMOBILE: Scan stoppage due to no delay argument given has been fixed, was a variable set way too high.

                // Get the signal level. Taking average from 5 samples doesn't have any benefits. Just adding more delay.
                GetSignalLevelEx(sockfd, &level, 1 );

                if (opt_verbose)
                {
                    printf("\rFreq: %s Signal: %2.2f Squelch: %2.2f", print_freq(current_freq), level, squelch);
                    fflush (stdout);
                }

                if(opt_squelch_delta_top > 0)
                {
                    chosen_squelch = &Frequencies[i].squelch_delta_top;
                }

                if (opt_udp_listen)
                    udp_signal = IsThereASignal(udp_sockfd);

                if (( level >= *chosen_squelch ) || udp_signal)
                {
                    // reassure that it's a valid signal
                    // GetSignalLevel() overshoot mitigation
                    // That's because there's no way of telling after sending GetSignalLevel() command
                    // if the signal is bound to that frequency or not.

                    bool skip_mitigation = false;
                    for (size_t j = 1; j < PREV_FREQ_MAX; j++)
                    {
                        usleep(350000);
                        GetSignalLevelEx(sockfd, &level, 5 );
                        if (opt_udp_listen)
                            udp_signal = IsThereASignal(udp_sockfd);
                        if (( level < squelch ) || udp_signal)
                        {
                            i = indexes[j];
                            current_freq = FilterFrequency(i);
                            SetFreq(sockfd, current_freq);
                            if (j == PREV_FREQ_MAX - 1)
                            {
                                skip_mitigation = true;
                            }
                        }
                        else
                        {
                            break;
                        }
                    }
                    if (skip_mitigation)
                    {
                        continue;
                    }

                    if (opt_verbose)
                        printf("\n");

                    time_t hit_time = GetTime(timestamp);
                    if (opt_squelch_delta_auto_enable)
                    {
                        squelch_backup = squelch;
                        SetSquelchLevel(sockfd, Frequencies[i].noise_floor + squelch_delta);
                        printf ("\n[%s] Freq: %s active [%s],\nLevel: %2.2f/%2.2f, Squelch set: %2.2f ",
                                timestamp, print_freq(current_freq),
                                Frequencies[i].descr, level, squelch, Frequencies[i].noise_floor + squelch_delta);
                    }
                    else {
                        printf ("[%s] Freq: %s active [%s], Level: %2.2f/%2.2f ",
                                timestamp, print_freq(current_freq),
                                Frequencies[i].descr, level, squelch);
                    }
                    fflush(stdout);
                    skip = WaitUserInputOrDelay(sockfd, opt_delay, &current_freq);
                    time_t elapsed = DiffTime(timestamp, hit_time);
                    printf (" [elapsed time %s]\n", timestamp);
                    if (opt_squelch_delta_auto_enable) SetSquelchLevel(sockfd, squelch_backup);
                    fflush(stdout);
                }
                else {
                    skip = false;
                }
            }
        }
    }
}

//
// Save frequency found
//
bool SaveFreq(freq_t freq_current)
{
    bool found = false;
    freq_t tollerance = 5000; // 10Khz bwd

    int  temp_count = 0;
    freq_t delta, temp_delta = tollerance;
    int  temp_i     = 0;

    for (int i = 0; i < SavedFreq_Max; i++)
    {
        // Find a previous hit with some tollerance
        if (freq_current >= (SavedFrequencies[i].freq - tollerance) &&
            freq_current <  (SavedFrequencies[i].freq + tollerance)   )
        {
            // found match, loop for minimum delta
            delta = freq_current - SavedFrequencies[i].freq;
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
            SavedFreq_Max = 0; // restart from scratch ?
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
//
bool Debounce (int sockfd, freq_t current_freq, double level)
{
    double current_level = level;
    double squelch;
    usleep(300000); // 300 ms wait, hope it's good enough
    GetSignalLevelEx( sockfd, &current_level, 5 );
    GetSquelchLevel ( sockfd, &squelch );

    if (current_level < squelch )
        return false; // signal lost or ghost
    else
        return true;

}

//
// BacktrackFrequency
// got a signal but lost it
// move back to find it again more slowly
//
freq_t BacktrackFrequency(int sockfd, freq_t current_freq, freq_t freq_interval, int numberOfIntervals, freq_t freq_min, freq_t freq_max)
{
    double squelch = 0;
    double level = 0;
    int i;

    for (i=0 ; i < numberOfIntervals ; i++)
    {
        current_freq -= freq_interval;
        if (current_freq < freq_min)
            current_freq = freq_max - freq_interval ;
        GetSquelchLevel(sockfd, &squelch);
        SetFreq(sockfd, current_freq);
        usleep(150000);
        // tries to average out spikes, 5 sample
        GetSignalLevelEx( sockfd, &level, 5);
        if (level >= squelch)
        {
            //found it again
            break;
        }
    }
    return current_freq;
}

//
// AdjustFrequency
// Fine tuning to reach max level
// Perform a sweep between -15+15Khz around current_freq with 5kHz steps
// Return the found frequency
//
freq_t AdjustFrequency(int sockfd, freq_t current_freq, freq_t freq_interval)
{
    freq_t freq_min   = current_freq - 10000;
    freq_t freq_max   = current_freq + 10000;
    freq_t freq_steps = freq_interval;
    long max_levels = (freq_max - freq_min) / freq_steps ;
    typedef struct { double level; freq_t freq; } LEVELS;
    LEVELS levels[max_levels];
    int    l = 0;
    double squelch = 0;

    GetSquelchLevel(sockfd, &squelch);

    double level = 0;
    for (current_freq = freq_min; current_freq < freq_max; current_freq += freq_steps)
    {
        SetFreq(sockfd, current_freq);
        usleep(150000);
        // tries to average out spikes, 5 sample
        GetSignalLevelEx( sockfd, &level, 5);
        levels[l].level = level;
        levels[l].freq  = current_freq;
        //printf ("Freq:%ld Level:%.2f\t", current_freq, level);
        //fflush(stdout);
        l++;
    }

    double current_level = levels[0].level;
    double previous_level = current_level;
    int    start = 0;
    int    end   = max_levels;
    for (l = 0; l < max_levels; l++)
    {
        if (levels[l].level >= current_level)
        {
            current_level = levels[l].level;
            start = end = l;
        }
    }

    l = start + (end-start)/2;
    current_freq = levels[l].freq;
    SetFreq(sockfd, current_freq);
    usleep(150000);

    // before second pass fine tuning we check if the frequency is already known (and tuned with a mean value computed)
    // See SaveFreq
    freq_t tollerance = 7000;
    bool found = false;
    for (int i = 0; i < SavedFreq_Max; i++)
    {
        // Find a previous hit with some tollerance
        if (current_freq >= (SavedFrequencies[i].freq - tollerance) &&
            current_freq <  (SavedFrequencies[i].freq + tollerance)   )
        {
            if (SavedFrequencies[i].count > 4) // 4 fine tuned frequency is good enough to have a candidate freq
            {
                current_freq = SavedFrequencies[i].freq;
                found = true;
                break;
            }
        }
    }

    if (found)
    {
        SetFreq(sockfd, current_freq);
        usleep(150000);
        // Cheating: here I return the rough value from the first pass to avoid stucking on possibly wrong freq.
        return levels[l].freq;
    }

    // Second pass - Fine tuning + - 5Khz (freq_steps input) with 1 khz steps
    // Dived in two half, follow one until the level decreases if so follow the second half
    // (hopefully this reduces the num of steps), tries to average out spikes, 3 sample
    double reference_level;
    GetSignalLevelEx( sockfd, &reference_level, 5);
    freq_t   reference_freq  = current_freq;
    freq_min   = current_freq - 5000;
    freq_max   = current_freq + 5000;
    freq_steps = 1000; // 1 Khz fine tuning
    max_levels = (freq_max - freq_min) / freq_steps ;
    LEVELS levels2[max_levels];
    l = 0;
    // upper half
    for (current_freq = reference_freq + freq_steps; current_freq < freq_max; current_freq += freq_steps)
    {
        SetFreq(sockfd, current_freq);
        usleep(150000);
        // tries to average out spikes, 5 sample
        GetSignalLevelEx( sockfd, &level, 5);

        if (level < reference_level)
        {
            // this way the signal is decreasing, stop
            break;
        }
        levels2[l].level = level;
        levels2[l].freq  = current_freq;
        //printf ("Freq:%ld Level:%.2f\t", current_freq, level);
        //fflush(stdout);
        l++;
    }
    // lower half
    for (current_freq = reference_freq - freq_steps; current_freq >= freq_min; current_freq-= freq_steps )
    {
        SetFreq(sockfd, current_freq);
        usleep(150000);
        // tries to average out spikes, 5 sample
        GetSignalLevelEx( sockfd, &level, 5);

        if (level < reference_level)
        {
            // this way signal is decreasing, stop
            break;
        }
        levels2[l].level = level;
        levels2[l].freq  = current_freq;
        //printf ("Freq:%ld Level:%.2f\t", current_freq, level);
        //fflush(stdout);
        l++;
    }
    // If no candidates found
    if (l == 0) // we are good, already in the middle
    {
        SetFreq(sockfd, reference_freq);
        return reference_freq;
    }

    // Here we have levels2 from 0 to n the first half and from n to l the second half
    // Find out the maximum level frequency
    current_level = levels[0].level;
    previous_level = current_level;
    start = 0;
    end   = max_levels;
    for (int i = 0; i < l; i++)
    {
        if (levels2[i].level > current_level)
        {
            current_level = levels2[i].level;
            start = end = i;
        }
    }
    l = start;
    current_freq = levels2[l].freq;
    SetFreq(sockfd, current_freq);

    return current_freq;

}

bool ScanFrequenciesInRange(int sockfd, freq_t freq_min, freq_t freq_max, freq_t freq_interval, double squelch_delta)
{
    freq_t freq = 0;
    GetCurrentFreq(sockfd, &freq);
    double level = 0;
    GetSignalLevel(sockfd, &level );
    double squelch = 0;
    double squelch_backup = 0;
    GetSquelchLevel(sockfd, &squelch);

    size_t freqeuencies_count = ((opt_max_freq-opt_min_freq)/opt_scan_bw); //for loop boundary

    freq_t current_freq = freq_min;

    SetFreq(sockfd, freq_min);
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
    double * chosen_squelch = NULL;

    if (opt_squelch_delta_top == 0.0)
        chosen_squelch = &squelch;

    while (true)
    {
        for ( size_t i = 0 ; i < freqeuencies_count; i++)
        {
            CheckUserInput();

            IsBannedFreq(&current_freq); // test and change current_frequency to next available slot;
            SetFreq(sockfd, current_freq);
            if (saved_cycle)
                usleep((skip)?sleep_cycle_active:sleep_cyle_saved);
            else
                usleep((skip)?sleep_cycle_active:sleep_cyle);

            GetSquelchLevel(sockfd, &squelch);
            GetSignalLevelEx(sockfd, &level, 5 );

            if (Frequencies[i].noise_floor == 0)
            {
                Frequencies[i].noise_floor = level;
                Frequencies[i].squelch_delta_top = Frequencies[i].noise_floor + opt_squelch_delta_top;
            }

            if (opt_verbose)
            {
                printf("\rFreq: %s Signal: %2.2f Squelch: %2.2f", print_freq(current_freq), level, squelch);
                fflush (stdout);
            }

            if (opt_squelch_delta_top > 0)
            {
                chosen_squelch = &Frequencies[i].squelch_delta_top;
            }

            if (level >= *chosen_squelch)
            {
                if (opt_verbose){
                    printf("\n");
                }
                // we have a possible match, but sometimes level oscillates after a squelch miss
                bool still_good = Debounce(sockfd, current_freq, level);
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
                    if (!saved_cycle)
                    {
                        current_freq = BacktrackFrequency(sockfd, current_freq, freq_interval, 4, freq_min, freq_max);
                        if (IsBannedFreq(&current_freq))
                        {
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
                current_freq = AdjustFrequency(sockfd, current_freq, freq_interval/2);
                if (IsBannedFreq(&current_freq))
                {
                    skip = true;
                }
                else
            {
                    SaveFreq(current_freq);
                    if (opt_squelch_delta_auto_enable){
                        squelch_backup = squelch;
                        SetSquelchLevel(sockfd, Frequencies[i].noise_floor + squelch_delta);
                    }

                    time_t hit_time = GetTime(timestamp);
                    if (opt_squelch_delta_auto_enable)
                    {
                        printf ("\n[%s] Freq: %s active,\nLevel: %2.2f/%2.2f, Squelch set: %2.2f ",
                                timestamp, print_freq(current_freq),
                                level, squelch, Frequencies[i].noise_floor + squelch_delta);
                    }
                    else
                {
                        printf ("[%s] Freq: %s active, Level: %2.2f/%2.2f ",
                                timestamp, print_freq(current_freq),
                                level, squelch );
                    }
                    fflush(stdout);
                    // Wait user input or delay time after signal lost
                    skip = WaitUserInputOrDelay(sockfd, opt_delay, &current_freq);
                    time_t elapsed = DiffTime(timestamp, hit_time);
                    printf (" [elapsed time %s]\n", timestamp);
                    fflush(stdout);
                    if (opt_squelch_delta_auto_enable) SetSquelchLevel(sockfd, squelch_backup);
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
                Frequencies[i].noise_floor = (Frequencies[i].noise_floor + level)/2;
                Frequencies[i].squelch_delta_top = Frequencies[i].noise_floor + opt_squelch_delta_top;
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
                    current_freq = SavedFrequencies[saved_idx].freq;
                    current_saved_idx = saved_idx;
                    saved_idx++;
                    if (saved_idx >= SavedFreq_Max)
                        saved_idx = 0;
                    continue;
                }
            }
            current_freq+=freq_interval;
            if (current_freq > freq_max)
                current_freq = freq_min;
            sweep_count++;
        }
    }
}


int main(int argc, char **argv) {
    int sockfd, portno, n, udp_sockfd;
    char *hostname;
    char buf[BUFSIZE];
    FILE *bookmarksfd;

    opt_hostname        = (char *) g_hostname;
    opt_port            = g_portno;
    opt_udp_listen_port = g_udp_listen_portno;
    opt_delay           = g_delay;
    ParseInputOptions(argc, argv);

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
    if (opt_udp_listen)
    {
        printf ("UDP listening on port %d.\n", opt_port);
        udp_sockfd = UdpConnect(opt_hostname, opt_udp_listen_port);
    }

    if (!opt_tag_search) // sweep or bookmark
    {
        if (opt_min_freq == 0 && opt_max_freq == 0)
        {
            freq_t current_freq;
            GetCurrentFreq(sockfd, &current_freq);
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

    if (opt_scan_mode == sweep)
    {
        size_t freqeuencies_count = (size_t)(((opt_max_freq-opt_min_freq)/opt_scan_bw)+1);
        Frequencies = malloc(freqeuencies_count*sizeof(FREQ));
    }
    else {
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

    if (opt_scan_mode == sweep) {
        ScanFrequenciesInRange(sockfd, opt_min_freq, opt_max_freq, opt_scan_bw, opt_squelch_delta_bottom);
    }
    else {
        ScanBookmarkedFrequenciesInRange(sockfd, udp_sockfd, opt_min_freq, opt_max_freq, opt_squelch_delta_bottom);
    }

    fclose (bookmarksfd);
    close(sockfd);
    close(udp_sockfd);
    free(Frequencies);
    return 0;
}
