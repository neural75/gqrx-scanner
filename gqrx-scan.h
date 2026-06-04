#ifndef GQRX_SCAN_H
#define GQRX_SCAN_H

#include "gqrx-prot.h"
#include <stdio.h>
#include <stdbool.h>

/* Frequency entry — used in Frequencies[], SavedFrequencies[], BannedFrequencies[] */
typedef struct {
    freq_t freq;
    double noise_floor;
    int count;
    int miss;
    char descr[BUFSIZE];
    char *tags[TAG_MAX];
    int   tag_max;
} FREQ;

/* Scan mode */
typedef enum { sweep, bookmark } SCAN_MODE;

/* Global arrays */
extern FREQ* Frequencies;
extern int   Frequencies_Max;
extern FREQ  SavedFrequencies[SAVED_FREQ_MAX];
extern int   SavedFreq_Max;
extern FREQ  BannedFrequencies[SAVED_FREQ_MAX];
extern int   BannedFreq_Max;

/* Option globals */
extern bool      opt_tag_search;
extern char     *opt_tags[TAG_MAX];
extern int       opt_tag_max;
extern freq_t    opt_min_freq;
extern freq_t    opt_max_freq;
extern freq_t    opt_scan_bw;
extern long      opt_delay;
extern long      opt_speed;
extern long      opt_date;
extern SCAN_MODE opt_scan_mode;
extern bool      opt_record;
extern bool      opt_verbose;
extern double    opt_squelch_delta;
extern bool      opt_squelch_delta_auto_enable;

/* Functions */
extern bool   LoadFrequencies(FILE *bookmarksfd);
extern bool   prefix(const char *pre, const char *str);
extern char  *print_freq(freq_t freq);
extern bool   ParseTags(char *tags);
extern bool   SaveFreq(freq_t freq_current);
extern bool   BanFreq(freq_t freq_current);
extern bool   IsBannedFreq(freq_t *freq_current);
extern void   ClearAllBans(void);
extern freq_t FilterFrequency(int idx);
extern bool   ScanFrequenciesInRange(int sockfd, freq_t freq_min, freq_t freq_max, freq_t freq_interval, double squelch_delta);
extern bool   ScanBookmarkedFrequenciesInRange(int sockfd, freq_t freq_min, freq_t freq_max, double squelch_delta);

/* Global state management */
void SetOptDefaults(void);
void FreeFrequencies(void);
void ResetOptTags(void);

/* Test infrastructure (TESTING_BUILD only) */
#ifdef TESTING_BUILD
extern int g_testing_max_full_sweeps;
extern int g_testing_sweep_full_count;
extern int g_testing_max_bookmark_loops;
extern int g_testing_bookmark_loop_count;
#endif

#endif /* GQRX_SCAN_H */
