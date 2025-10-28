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
extern bool LoadFrequencies(FILE *bookmarksfd);

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

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_load_frequencies_from_file),
        cmocka_unit_test(test_load_frequencies_empty_file),
        cmocka_unit_test(test_frequency_tags_parsing),
        cmocka_unit_test(test_frequency_field_parsing),
    };
    
    return cmocka_run_group_tests(tests, NULL, NULL);
}
