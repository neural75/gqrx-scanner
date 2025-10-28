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
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

#include "../gqrx-prot.h"

/* FREQ type definition from gqrx-scan.c */
typedef struct {
    freq_t freq;
    double noise_floor;
    int count;
    int miss;
    char descr[BUFSIZE];
    char *tags[TAG_MAX];
    int tag_max;
} FREQ;

/* External declarations from gqrx-scan.c */
extern FREQ* Frequencies;
extern int Frequencies_Max;
extern FREQ SavedFrequencies[SAVED_FREQ_MAX];
extern int SavedFreq_Max;
extern FREQ BannedFrequencies[SAVED_FREQ_MAX];
extern int BannedFreq_Max;
extern char *opt_tags[TAG_MAX];
extern int opt_tag_max;

extern bool LoadFrequencies(FILE *bookmarksfd);
extern bool prefix(const char *pre, const char *str);
extern char *print_freq(freq_t freq);
extern bool ParseTags(char *tags);
extern bool SaveFreq(freq_t freq_current);
extern bool BanFreq(freq_t freq_current);
extern bool IsBannedFreq(freq_t *freq_current);
extern void ClearAllBans(void);

/* ========================================================================
 * Utility Tests (from test_utils.c)
 * ======================================================================== */

static void test_prefix_match(void **state)
{
    (void) state;
    assert_true(prefix("Hello", "Hello World"));
    assert_true(prefix("# ", "# Comment"));
}

static void test_prefix_no_match(void **state)
{
    (void) state;
    assert_false(prefix("Goodbye", "Hello World"));
    assert_false(prefix("Test", "# Comment"));
}

static void test_prefix_empty(void **state)
{
    (void) state;
    assert_true(prefix("", "Any String"));
    assert_true(prefix("", ""));
}

static void test_prefix_exact_match(void **state)
{
    (void) state;
    assert_true(prefix("Match", "Match"));
}

/* ========================================================================
 * File Operations Tests (from test_file_ops.c)
 * ======================================================================== */

static void test_load_frequencies_from_file(void **state)
{
    (void) state;
    
    /* Allocate memory for Frequencies array */
    Frequencies = malloc(FREQ_MAX * sizeof(FREQ));
    assert_non_null(Frequencies);
    
    FILE *fp = fopen("tests/fixtures/test_bookmarks.csv", "r");
    assert_non_null(fp);
    
    LoadFrequencies(fp);
    fclose(fp);
    
    assert_int_equal(Frequencies_Max, 6);
    assert_int_equal(Frequencies[0].freq, 430037000);
    assert_string_equal(Frequencies[0].descr, " Beigua                   ");
    
    /* Clean up */
    for (int i = 0; i < Frequencies_Max; i++) {
        for (int k = 0; k < Frequencies[i].tag_max; k++) {
            free(Frequencies[i].tags[k]);
        }
    }
    free(Frequencies);
}

static void test_load_frequencies_empty_file(void **state)
{
    (void) state;
    
    /* Allocate memory for Frequencies array */
    Frequencies = malloc(FREQ_MAX * sizeof(FREQ));
    assert_non_null(Frequencies);
    
    FILE *fp = tmpfile();
    assert_non_null(fp);
    
    Frequencies_Max = 0;
    LoadFrequencies(fp);
    fclose(fp);
    
    assert_int_equal(Frequencies_Max, 0);
    
    /* Clean up */
    free(Frequencies);
}

static void test_frequency_tags_parsing(void **state)
{
    (void) state;
    
    /* Allocate memory for Frequencies array */
    Frequencies = malloc(FREQ_MAX * sizeof(FREQ));
    assert_non_null(Frequencies);
    
    FILE *fp = fopen("tests/fixtures/test_bookmarks.csv", "r");
    assert_non_null(fp);
    
    LoadFrequencies(fp);
    fclose(fp);
    
    /* Test first frequency (430037000) with tags "DMR, VHF" */
    assert_int_equal(Frequencies[0].tag_max, 2);
    assert_string_equal(Frequencies[0].tags[0], "DMR");
    assert_string_equal(Frequencies[0].tags[1], "VHF");
    
    /* Test second frequency (430288000) with tag "DMR" */
    assert_int_equal(Frequencies[1].tag_max, 1);
    assert_string_equal(Frequencies[1].tags[0], "DMR");
    
    /* Test fourth frequency (430900000) with tags "DMR, Radio Links" */
    assert_int_equal(Frequencies[3].tag_max, 2);
    assert_string_equal(Frequencies[3].tags[0], "DMR");
    assert_string_equal(Frequencies[3].tags[1], "Radio Links");
    
    /* Test fifth frequency (144500000) with tag "VHF" */
    assert_int_equal(Frequencies[4].tag_max, 1);
    assert_string_equal(Frequencies[4].tags[0], "VHF");
    
    /* Clean up */
    for (int i = 0; i < Frequencies_Max; i++) {
        for (int k = 0; k < Frequencies[i].tag_max; k++) {
            free(Frequencies[i].tags[k]);
        }
    }
    free(Frequencies);
}

static void test_frequency_field_parsing(void **state)
{
    (void) state;
    
    /* Allocate memory for Frequencies array */
    Frequencies = malloc(FREQ_MAX * sizeof(FREQ));
    assert_non_null(Frequencies);
    
    FILE *fp = fopen("tests/fixtures/test_bookmarks.csv", "r");
    assert_non_null(fp);
    
    LoadFrequencies(fp);
    fclose(fp);
    
    /* Verify correct number of frequencies loaded */
    assert_int_equal(Frequencies_Max, 6);
    
    /* Test all frequencies are parsed correctly */
    assert_int_equal(Frequencies[0].freq, 430037000);
    assert_int_equal(Frequencies[1].freq, 430288000);
    assert_int_equal(Frequencies[2].freq, 430887000);
    assert_int_equal(Frequencies[3].freq, 430900000);
    assert_int_equal(Frequencies[4].freq, 144500000);
    assert_int_equal(Frequencies[5].freq, 145000000);
    
    /* Verify frequencies are 64-bit values (freq_t is unsigned long long) */
    assert_true(sizeof(Frequencies[0].freq) == sizeof(unsigned long long));
    
    /* Test edge case: verify frequencies with leading spaces are trimmed */
    /* All frequencies in our test file have leading spaces before the number */
    assert_true(Frequencies[0].freq > 0);
    assert_true(Frequencies[0].freq < 1000000000000ULL); /* reasonable range check */
    
    /* Clean up */
    for (int i = 0; i < Frequencies_Max; i++) {
        for (int k = 0; k < Frequencies[i].tag_max; k++) {
            free(Frequencies[i].tags[k]);
        }
    }
    free(Frequencies);
}

/* ========================================================================
 * Utility Function Tests (print_freq, ParseTags, etc.)
 * ======================================================================== */

static void test_print_freq_ghz_range(void **state)
{
    (void) state;
    
    /* Test GHz range formatting */
    freq_t freq = 1234567890000ULL; /* 1234.567.890 GHz */
    char *result = print_freq(freq);
    
    assert_non_null(result);
    assert_string_equal(result, "1234.567.890 GHz");
}

static void test_print_freq_mhz_range(void **state)
{
    (void) state;
    
    /* Test MHz range formatting */
    freq_t freq = 145000000; /* 145.000 MHz */
    char *result = print_freq(freq);
    
    assert_non_null(result);
    assert_string_equal(result, "145.000 MHz");
    
    /* Test another MHz value */
    freq = 430037000; /* 430.037 MHz */
    result = print_freq(freq);
    assert_string_equal(result, "430.037 MHz");
}

static void test_print_freq_khz_range(void **state)
{
    (void) state;
    
    /* Test KHz range formatting */
    freq_t freq = 10000; /* 10 KHz */
    char *result = print_freq(freq);
    
    assert_non_null(result);
    assert_string_equal(result, "10 KHz");
}

static void test_print_freq_rounding(void **state)
{
    (void) state;
    
    /* Test rounding to nearest KHz */
    freq_t freq = 145000500; /* Should round to 145.001 MHz */
    char *result = print_freq(freq);
    
    assert_non_null(result);
    assert_string_equal(result, "145.001 MHz");
}

static void test_parse_tags_single(void **state)
{
    (void) state;
    
    /* Reset opt_tags */
    for (int i = 0; i < opt_tag_max; i++) {
        if (opt_tags[i]) {
            free(opt_tags[i]);
            opt_tags[i] = NULL;
        }
    }
    opt_tag_max = 0;
    
    /* Test single tag */
    char tags[] = "VHF";
    bool result = ParseTags(tags);
    
    assert_true(result);
    assert_int_equal(opt_tag_max, 1);
    assert_string_equal(opt_tags[0], "VHF");
    
    /* Clean up */
    for (int i = 0; i < opt_tag_max; i++) {
        free(opt_tags[i]);
        opt_tags[i] = NULL;
    }
    opt_tag_max = 0;
}

static void test_parse_tags_multiple(void **state)
{
    (void) state;
    
    /* Reset opt_tags */
    for (int i = 0; i < opt_tag_max; i++) {
        if (opt_tags[i]) {
            free(opt_tags[i]);
            opt_tags[i] = NULL;
        }
    }
    opt_tag_max = 0;
    
    /* Test multiple tags */
    char tags[] = "DMR|VHF|UHF";
    bool result = ParseTags(tags);
    
    assert_true(result);
    assert_int_equal(opt_tag_max, 3);
    assert_string_equal(opt_tags[0], "DMR");
    assert_string_equal(opt_tags[1], "VHF");
    assert_string_equal(opt_tags[2], "UHF");
    
    /* Clean up */
    for (int i = 0; i < opt_tag_max; i++) {
        free(opt_tags[i]);
        opt_tags[i] = NULL;
    }
    opt_tag_max = 0;
}

static void test_parse_tags_empty(void **state)
{
    (void) state;
    
    /* Reset opt_tags */
    for (int i = 0; i < opt_tag_max; i++) {
        if (opt_tags[i]) {
            free(opt_tags[i]);
            opt_tags[i] = NULL;
        }
    }
    opt_tag_max = 0;
    
    /* Test empty string (should fail) */
    char tags[] = "";
    bool result = ParseTags(tags);
    
    assert_false(result);
    assert_int_equal(opt_tag_max, 0);
}

static void test_save_freq_new(void **state)
{
    (void) state;
    
    /* Reset saved frequencies */
    SavedFreq_Max = 0;
    memset(SavedFrequencies, 0, sizeof(SavedFrequencies));
    
    /* Save a new frequency */
    freq_t freq = 145000000;
    bool result = SaveFreq(freq);
    
    assert_true(result);
    assert_int_equal(SavedFreq_Max, 1);
    assert_int_equal(SavedFrequencies[0].freq, freq);
    assert_int_equal(SavedFrequencies[0].count, 1);
    assert_int_equal(SavedFrequencies[0].miss, 0);
}

static void test_save_freq_duplicate_within_tolerance(void **state)
{
    (void) state;
    
    /* Reset saved frequencies */
    SavedFreq_Max = 0;
    memset(SavedFrequencies, 0, sizeof(SavedFrequencies));
    
    /* Save initial frequency */
    freq_t freq1 = 145000000;
    SaveFreq(freq1);
    
    /* Save frequency within tolerance (±5000 Hz) */
    freq_t freq2 = 145003000; /* 3 KHz away, within 5 KHz tolerance */
    SaveFreq(freq2);
    
    /* Should still have only 1 entry, with updated count */
    assert_int_equal(SavedFreq_Max, 1);
    assert_int_equal(SavedFrequencies[0].count, 2);
}

static void test_save_freq_lower_within_tolerance(void **state)
{
    (void) state;
    
    /* Reset saved frequencies */
    SavedFreq_Max = 0;
    memset(SavedFrequencies, 0, sizeof(SavedFrequencies));
    
    /* Save initial HIGHER frequency */
    freq_t freq1 = 145003000;
    SaveFreq(freq1);
    
    /* Save LOWER frequency within tolerance (this tests the negative delta bug) */
    freq_t freq2 = 145000000; /* 3 KHz lower, within 5 KHz tolerance */
    SaveFreq(freq2);
    
    /* Should still have only 1 entry, with updated count */
    /* This will FAIL with the current buggy code due to unsigned underflow */
    assert_int_equal(SavedFreq_Max, 1);
    assert_int_equal(SavedFrequencies[0].count, 2);
}

static void test_ban_freq(void **state)
{
    (void) state;
    
    /* Reset banned frequencies */
    BannedFreq_Max = 0;
    memset(BannedFrequencies, 0, sizeof(BannedFrequencies));
    
    /* Ban a frequency */
    freq_t freq = 145000000;
    bool result = BanFreq(freq);
    
    assert_true(result);
    assert_int_equal(BannedFreq_Max, 1);
    assert_int_equal(BannedFrequencies[0].freq, freq);
}

static void test_is_banned_freq(void **state)
{
    (void) state;
    
    /* Reset banned frequencies */
    BannedFreq_Max = 0;
    memset(BannedFrequencies, 0, sizeof(BannedFrequencies));
    
    /* Ban a frequency */
    freq_t banned = 145000000;
    BanFreq(banned);
    
    /* Test if banned frequency is detected */
    freq_t test_freq = 145000000;
    bool result = IsBannedFreq(&test_freq);
    
    assert_true(result);
    /* Frequency should be adjusted past the banned range */
    assert_true(test_freq > banned);
}

static void test_clear_all_bans(void **state)
{
    (void) state;
    
    /* Reset and add some banned frequencies */
    BannedFreq_Max = 0;
    memset(BannedFrequencies, 0, sizeof(BannedFrequencies));
    
    BanFreq(145000000);
    BanFreq(430000000);
    
    assert_int_equal(BannedFreq_Max, 2);
    
    /* Clear all bans */
    ClearAllBans();
    
    assert_int_equal(BannedFreq_Max, 0);
}

/* ========================================================================
 * Test Runner - All Tests Combined
 * ======================================================================== */

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Utility tests - prefix function */
        cmocka_unit_test(test_prefix_match),
        cmocka_unit_test(test_prefix_no_match),
        cmocka_unit_test(test_prefix_empty),
        cmocka_unit_test(test_prefix_exact_match),
        
        /* File operations tests */
        cmocka_unit_test(test_load_frequencies_from_file),
        cmocka_unit_test(test_load_frequencies_empty_file),
        cmocka_unit_test(test_frequency_tags_parsing),
        cmocka_unit_test(test_frequency_field_parsing),
        
        /* Utility function tests - print_freq */
        cmocka_unit_test(test_print_freq_ghz_range),
        cmocka_unit_test(test_print_freq_mhz_range),
        cmocka_unit_test(test_print_freq_khz_range),
        cmocka_unit_test(test_print_freq_rounding),
        
        /* Utility function tests - ParseTags */
        cmocka_unit_test(test_parse_tags_single),
        cmocka_unit_test(test_parse_tags_multiple),
        cmocka_unit_test(test_parse_tags_empty),
        
        /* Frequency management tests */
        cmocka_unit_test(test_save_freq_new),
        cmocka_unit_test(test_save_freq_duplicate_within_tolerance),
        cmocka_unit_test(test_save_freq_lower_within_tolerance),
        cmocka_unit_test(test_ban_freq),
        cmocka_unit_test(test_is_banned_freq),
        cmocka_unit_test(test_clear_all_bans),
    };
    
    return cmocka_run_group_tests(tests, NULL, NULL);
}
