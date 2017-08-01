/* 
 * gqrx-scan
 * A simple frequency scanner for gqrx
 * 
 * usage: gqrx-scan [-h|--host <host>] [-p|--port <port>] [-b|--bookmarks] [-s|--sweep] 
 *                  [-f <central frequency>] [-f <from freq>] [-t <to freq>]
 *                  [-d|--disable-sweep-store] [-s <min_hit_to_remember>] [-m <max_miss_to_forget>]
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
#include <linux/limits.h>
#include <math.h>

#define BUFSIZE         1024
#define FREQ_MAX        4096
#define SAVED_FREQ_MAX  1000
#define TAG_MAX         100

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


FREQ Frequencies[FREQ_MAX];
int  Frequencies_Max = 0;

FREQ SavedFrequencies[SAVED_FREQ_MAX];
int  SavedFreq_Max = 0;

/* 
 * error - wrapper for perror
 */
void error(char *msg) {
    perror(msg);
    exit(0);
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
        exit(0);
    }

    /* build the server's Internet address */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, 
	  (char *)&serveraddr.sin_addr.s_addr, server->h_length);
    serveraddr.sin_port = htons(portno);

    /* connect: create a connection with the server */
    if (connect(sockfd, (const struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0) 
      error("ERROR connecting");

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
      error("ERROR writing to socket");
    return true;
}

//
// Recv
//
bool Recv(int sockfd, char *buf)
{
    int n;

    n = read(sockfd, buf, BUFSIZE);
    if (n < 0) 
      error("ERROR reading from socket");
    buf[n]= '\0';
    return true;
}

//
// GQRX Protocol
//
bool GetCurrentFreq(int sockfd, long *freq)
{
    char buf[BUFSIZE];
    
    Send(sockfd, "f\n");
    Recv(sockfd, buf);

    if (strcmp(buf, "RPRT 1") == 0 )
        return false;
    
    sscanf(buf, "%ld", freq);
    return true;
}
bool SetFreq(int sockfd, long freq)
{
    char buf[BUFSIZE];
    
    sprintf (buf, "F %ld\n", freq);
    Send(sockfd, buf);
    Recv(sockfd, buf);

    if (strcmp(buf, "RPRT 1") == 0 )
        return false;
    
    long freq_current = 0;
    do
    {
        GetCurrentFreq(sockfd, &freq_current);
    } while (freq_current != freq);

    return true;
}

bool GetSignalLevel(int sockfd, double *dBFS)
{
    char buf[BUFSIZE];
    
    Send(sockfd, "l\n");
    Recv(sockfd, buf);

    if (strcmp(buf, "RPRT 1") == 0 )
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
    
    Send(sockfd, "l SQL\n");
    Recv(sockfd, buf);

    if (strcmp(buf, "RPRT 1") == 0 )
        return false;
    
    sscanf(buf, "%lf", dBFS);
    *dBFS = round((*dBFS) * 10)/10;

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
    *dBFS = *dBFS / (n_samp - errors);
    return true;
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
long * ReadFrequencies (FILE *bookmarksfd)
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
                    usleep(100000);
                    GetSignalLevelEx(sockfd, &level, 3 );
                    if (level >= squelch)
                    {
                        printf ("Freq: %ld MHz found (%s), Level:%.2f ", current_freq, Frequencies[i].descr, level);
                        fflush(stdout);
                    }
                    while (level > squelch) // someone is tx'ing
                    {
                        printf(".");
                        usleep(4000000);
                        GetSquelchLevel(sockfd, &squelch);
                        GetSignalLevelEx(sockfd, &level, 3 );
                        if (level < squelch)
                        {
                            printf (" Scanning...\n");
                        }
                        fflush(stdout);                         
                        
                    }
                    
                }
        }
        
    }

}

bool SaveFreq(long freq_current)
{
    bool found = false;
    for (int i = 0; i < SavedFreq_Max; i++)
    {
        if (freq_current == SavedFrequencies[i].freq)
        {
            // found exact match 
            SavedFrequencies[i].count++;
            SavedFrequencies[i].miss = 0;// reset miss count
            found = true;
            break;
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
    }

    return true;
}
//
// AdjustFrequency
// Fine tuning to reach max level
// Perform a sweep between -15+15Khz around current_freq with 5kHz steps
// Return found frequency
long AdjustFrequency(int sockfd, long current_freq, long freq_interval)
{
    long freq_min   = current_freq - 10000;
    long freq_max   = current_freq + 15000;
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
   
    // Second pass - Fine tuning + - 5Khz (freq_steps input) with 1 khz steps
    // Dived in two half, follow one until the level decreases if so follow the second half (hopefully this reduces the num of steps)
    // tries to average out spikes, 3 sample
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
    int max_miss_threshold = 10;  
    while (true)
    {
        SetFreq(sockfd, current_freq);
        if (saved_cycle)
            usleep(85000);
        else
            usleep(50000);
        GetSquelchLevel(sockfd, &squelch);
        GetSignalLevelEx(sockfd, &level, 3 );
        if (level >= squelch)
        {
            SaveFreq(current_freq);
            current_freq = AdjustFrequency(sockfd, current_freq, freq_interval/2);
            printf ("Freq: %ld MHz found", current_freq);
            fflush(stdout);
        }
        else
        {
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
        while (level >= squelch) // someone is tx'ing
        {
            printf(".");
            usleep(2000000);
            GetCurrentFreq(sockfd, &current_freq);
            GetSquelchLevel(sockfd, &squelch);
            GetSignalLevel(sockfd, &level );
            if (level < squelch)
            {
                // restart scanning
                // round up to near khz  145892125 -> 145892000
                current_freq = ( current_freq / 10000 ) * 10000; 
                
                printf (" Scanning...\n");
            }
            fflush(stdout);    
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

    /* check command line arguments */
    if (argc != 3) {
       fprintf(stderr,"usage: %s <hostname> <port>\n", argv[0]);
       exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);

    sockfd = Connect(hostname, portno);

    bookmarksfd = Open("~/.config/gqrx/bookmarks.csv");
    ReadFrequencies (bookmarksfd);
    
    bzero(SavedFrequencies, sizeof(SavedFrequencies));


    long freq_min =  27200000;
    long freq_max =  28800000;


    //ScanBookmarkedFrequenciesInRange(sockfd, freq_min, freq_max);
    ScanFrequenciesInRange(sockfd, freq_min, freq_max, 10000);



    close(sockfd);
    return 0;
}
