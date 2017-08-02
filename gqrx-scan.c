/* 
 * gqrx-scan
 * A simple frequency scanner for gqrx
 * 
 * usage: gqrx-scan [-h|--host <host>] [-p|--port <port>] [-b|--bookmarks] [-s|--sweep] 
 *                  [-f <central frequency>] [-f <from freq>] [-t <to freq>]
 *                  [-d|--disable-sweep-store] [-s <min_hit_to_remember>] [-m <max_miss_to_forget>]
 */
#include <stdio.h>
#include <stdio_ext.h>
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
#include <linux/limits.h>
#include <termios.h>
#include <time.h>

#include "gqrx-prot.h"

#define NB_ENABLE    true
#define NB_DISABLE   false

//
// Globals
//
typedef struct {
    long freq; // frequency in Mhz
    int count; // hit count on sweep scan
    int miss;  // miss count on sweep scan
    char descr[BUFSIZE]; // ugly buffer for descriptions: TODO convert it in a pointer
    char *tags[TAG_MAX]; // tags 
}FREQ;


FREQ Frequencies[FREQ_MAX] = {0};
int  Frequencies_Max = 0;

FREQ SavedFrequencies[SAVED_FREQ_MAX] = {0};
int  SavedFreq_Max = 0;

FREQ BannedFrequencies[SAVED_FREQ_MAX] = {0};
int  BannedFreq_Max = 0;

//
// Defaults
//
const char      *g_hostname     = "localhost";
const int       g_portno        = 7356;
const long      g_freq_delta    = 1000000; // +- Mhz Bandwidth to scan from current freq. 
const long      g_ban_tollerance    = 10000; // +- Khz bandwidth to ban from current freq.

//
// Local Prototypes
//
bool BanFreq (long freq_current);

//
// Utilities
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
// GetTimeStamp
// Get the time stamp dd-mm-yy hh:mm:ss
//
time_t GetTimeStamp(char *timestamp)
{
    time_t etime = time(NULL);
    struct tm *ltime = localtime (&etime);
    sprintf(timestamp, "%2.2d-%2.2d-%2.2d %2.2d:%2.2d:%2.2d", ltime->tm_mday, ltime->tm_mon+1, ltime->tm_year%100,
            ltime->tm_hour, ltime->tm_min, ltime->tm_sec);
    return etime;
}
// Calculate difference in time in [dd days][hh:][mm:][ss secs]
time_t DiffTime(char *timestamp, time_t start_time)
{
    double seconds;
    time_t etime = time (NULL);
    seconds = difftime(etime , start_time);

    time_t elapssed = (time_t)seconds; //aerrg
    struct tm *ltime = localtime(&elapssed);

    timestamp[0] = '\0';
    if (ltime->tm_mday > 1)
    {
        char days[10];
        sprintf(days, "%2d days ", ltime->tm_mday-1);
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
        char min[10];
        sprintf(min, "%2.2d:", ltime->tm_min);
        strcat(timestamp, min);
    }
    if (ltime->tm_sec > 0)
    {
        char sec[10];
        sprintf(sec, "%2.2d sec", ltime->tm_sec);
        strcat(timestamp, sec);
    }
    return elapssed;
}

//
// WaitUserInputOrDelay
// Waits for user input or a delay after the carrier is gone
// Returns if the user has pressed <space> or <enter> to skip frequency
//
bool WaitUserInputOrDelay (int sockfd, long delay, long *current_freq)
{
    double    squelch;
    double  level;
    long    sleep_time = 0, sleep = 100000; // 100 ms
    int     exit = 0;
    char    c;
    bool    skip = false;
    
    __fpurge(stdin);
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
            // <space> or <enter> ?
            if ( (c == ' ') || (c == '\n') )
            {
                exit = 1; // exit
                skip = true;
                break; 
            }
            else if ( c == 'b' )
            {
                // Ban a frequency
                BanFreq(*current_freq);
                exit = 1;
                skip = true;
                break;
            }
            else
            {
                exit = 0;
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
    *current_freq = ceil( *current_freq / 10000.0 ) * 10000.0; 
    
    __fpurge(stdin);
    return skip; 
}

//
// Open
//
FILE * Open (char * filename)
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
        error("ERROR opening gqrx bookmark file");

    return filefd;
}

bool prefix(const char *pre, const char *str)
{
    return strncmp(pre, str, strlen(pre)) == 0;
}

//
// ReadFrequencies from gqrx file format
//
bool ReadFrequencies (FILE *bookmarksfd)
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
            sscanf(token, "%ld", &Frequencies[i].freq);
            token = strtok(NULL, ";"); // descr
            strncpy(Frequencies[i].descr, token , BUFSIZE);
            token = strtok(NULL, ";"); // mode
            token = strtok(NULL, ";"); // bw
            token = strtok(NULL, ";"); // tags, comma separated

            char * tag = strtok(token,",\n");
            int k = 0;
            while (tag != NULL && k < TAG_MAX)
            {
                sscanf (tag, "%ms", &Frequencies[i].tags[k]);
                printf("%s ", Frequencies[i].tags[k]);

                k++;
                tag = strtok (NULL, ",\n");
            }

            printf(":%ld: %s\n", Frequencies[i].freq, Frequencies[i].descr);
            i++;
        }
    }
    Frequencies_Max = i;
    return true;
}


bool ScanBookmarkedFrequenciesInRange(int sockfd, long freq_min, long freq_max)
{
    long freq = 0;
    GetCurrentFreq(sockfd, &freq);
    double level = 0;
    GetSignalLevel(sockfd, &level );
    double squelch = 0;
    GetSquelchLevel(sockfd, &squelch);

    long current_freq = freq_min;

    SetFreq(sockfd, freq_min);
    bool skip = false;
    long sleep_cycle_active = 500000; // skipping from active frequency need more time to wait squelch level to kick in
    long sleep_cyle_saved   = 85000 ; // skipping freqeuency need more time to get signal level
    char timestamp[BUFSIZE] = {0};
    while (true)
    {
        for (int i = 0; i < Frequencies_Max; i++)
        {
            current_freq = Frequencies[i].freq;
            if (( current_freq > freq_min) &&
                ( current_freq < freq_max)    )
                {
                    // Found a bookmark in the range
                    SetFreq(sockfd, current_freq);
                    GetSquelchLevel(sockfd, &squelch);
                    usleep((skip)?sleep_cycle_active:sleep_cyle_saved);
                    GetSignalLevelEx(sockfd, &level, 3 );
                    if (level >= squelch)
                    {
                        time_t hit_time = GetTimeStamp(timestamp);
                        printf ("%s - Freq: %ld MHz active [%s], Level:%.2f/%.2f ", timestamp, current_freq, Frequencies[i].descr, level, squelch);
                        fflush(stdout);
                        skip = WaitUserInputOrDelay(sockfd, 2000000, &current_freq);
                        time_t elapsed = DiffTime(timestamp, hit_time);
                        printf (" [elapsed time %s]\n", timestamp);
                        fflush(stdout);
                    }
                    else    
                        skip = false;
                    
                }
        }
        
    }

}

//
// Save frequency found
//
bool SaveFreq(long freq_current)
{
    bool found = false;
    long tollerance = 5000; // 10Khz bwd

    int  temp_count = 0;
    long delta, temp_delta = tollerance;
    int  temp_i     = 0;

    for (int i = 0; i < SavedFreq_Max; i++)
    {
        // Find a previous hit with some tollerance
        if (freq_current >= (SavedFrequencies[i].freq - tollerance) &&
            freq_current <  (SavedFrequencies[i].freq + tollerance)   )
        {
            // found match, loop for minimum delta
            delta = abs(freq_current - SavedFrequencies[i].freq); 
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
    SavedFrequencies[temp_i].freq = (( ((long long)SavedFrequencies[temp_i].freq * count ) + freq_current ) / (count + 1));
    SavedFrequencies[temp_i].count++;
    SavedFrequencies[temp_i].miss = 0;// reset miss count
    

    return true;
}

//
// Ban a frequency found 
//
bool BanFreq (long freq_current)
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
// TestFreq
// Test whether a frequency is banned or not
//
bool IsBannedFreq (long *freq_current)
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
            return true;
        }        
    }    
    if (i >= BannedFreq_Max)
        return false;
}

//
// AdjustFrequency
// Fine tuning to reach max level
// Perform a sweep between -15+15Khz around current_freq with 5kHz steps
// Return the found frequency
//
long AdjustFrequency(int sockfd, long current_freq, long freq_interval)
{
    long freq_min   = current_freq - 10000;
    long freq_max   = current_freq + 10000;
    long freq_steps = freq_interval;
    long max_levels = (freq_max - freq_min) / freq_steps ;
    typedef struct { double level; long freq; } LEVELS;
    LEVELS levels[max_levels];
    int    l = 0;
    double squelch = 0;

    GetSquelchLevel(sockfd, &squelch);
    
    double level = 0;
    for (current_freq = freq_min; current_freq < freq_max; current_freq += freq_steps)
    {
        SetFreq(sockfd, current_freq);
        usleep(150000);
        // tries to average out spikes, 3 sample
        GetSignalLevelEx( sockfd, &level, 3);
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
    long tollerance = 7000;
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
    GetSignalLevelEx( sockfd, &reference_level, 3);       
    long   reference_freq  = current_freq;
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
        // tries to average out spikes, 3 sample
        GetSignalLevelEx( sockfd, &level, 3);

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
        // tries to average out spikes, 3 sample
        GetSignalLevelEx( sockfd, &level, 3);

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

bool ScanFrequenciesInRange(int sockfd, long freq_min, long freq_max, long freq_interval)
{
    long freq = 0;
    GetCurrentFreq(sockfd, &freq);
    double level = 0;
    GetSignalLevel(sockfd, &level );
    double squelch = 0;
    GetSquelchLevel(sockfd, &squelch);

    long current_freq = freq_min;

    SetFreq(sockfd, freq_min);
    int saved_idx = 0, current_saved_idx = 0;
    int sweep_count = 0;
    long last_freq;
    bool saved_cycle = false;
    // minimum hit threshold on frequency already seen (count): above this the freq is a candidate 
    int min_hit_threshold = 2;
    // maximum miss threshold on frequencies not seen anymore: above this the candidate count is decremented
    // note: during monitoring on active freq the counter can reach high values so this threshold should not be too high
    // otherwise the chance to exclude the freq is very low and will continue to monitor even after prolonged inactive time
    int max_miss_threshold = 20; 
    long sleep_cyle         = 10000 ; // wait 50ms after setting freq to get signal level 
    long sleep_cyle_saved   = 85000 ; // skipping freqeuency need more time to get signal level
    long sleep_cycle_active = 500000; // skipping from active frequency need more time to wait squelch level to kick in
    bool skip = false;   // user input
    char timestamp[BUFSIZE] = {0};

    while (true)
    {
        IsBannedFreq(&current_freq); // test and change current_frequency to next available slot; 
        SetFreq(sockfd, current_freq);
        if (saved_cycle)
            usleep((skip)?sleep_cycle_active:sleep_cyle_saved);
        else
            usleep((skip)?sleep_cycle_active:sleep_cyle);

        GetSquelchLevel(sockfd, &squelch);
        GetSignalLevelEx(sockfd, &level, 3 );
        if (level >= squelch)
        {
            current_freq = AdjustFrequency(sockfd, current_freq, freq_interval/2);
            SaveFreq(current_freq);    
            time_t hit_time = GetTimeStamp(timestamp);
            printf ("%s - Freq: %ld MHz active, Level:%.2f/%.2f ", timestamp, current_freq, level, squelch);
            fflush(stdout);
            // Wait user input or delay time after signal lost
            skip = WaitUserInputOrDelay(sockfd, 2000000, &current_freq);
            time_t elapsed = DiffTime(timestamp, hit_time);
            printf (" [elapsed time %s]\n", timestamp);
            fflush(stdout);
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

int main(int argc, char **argv) {
    int sockfd, portno, n;
    char *hostname;
    char buf[BUFSIZE];

    FILE *bookmarksfd;
#if 0
    /* check command line arguments */
    if (argc != 3) {
       fprintf(stderr,"usage: %s <hostname> <port>\n", argv[0]);
       exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);
#endif
    hostname = (char *) g_hostname;
    portno   = g_portno;


    sockfd = Connect(hostname, portno);

    bookmarksfd = Open("~/.config/gqrx/bookmarks.csv");
    ReadFrequencies (bookmarksfd);
    

    long freq_min =  430000000;
    long freq_max =  431600000;

    long current_freq;
    GetCurrentFreq(sockfd, &current_freq);
    freq_min = current_freq - g_freq_delta;
    freq_max = current_freq + g_freq_delta;

    //ScanBookmarkedFrequenciesInRange(sockfd, freq_min, freq_max);
    ScanFrequenciesInRange(sockfd, freq_min, freq_max, 10000);


    fclose (bookmarksfd);
    close(sockfd);
    return 0;
}
