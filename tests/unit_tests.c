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

#include "../src/gqrx-scan.h"
#ifdef HAVE_WRAP_SOCKET_MOCKS
#include "mock_socket.h"

/* Non-static scan functions not declared in gqrx-scan.h */
extern bool   Debounce(freq_t current_freq, double level);
extern bool   BacktrackFrequency(freq_t current_freq,
                                 freq_t freq_interval, int numberOfIntervals,
                                 freq_t freq_min, freq_t freq_max,
                                 freq_t *out_freq, double *out_level);
#endif

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
    
    Frequencies = calloc(FREQ_MAX, sizeof(FREQ));
    assert_non_null(Frequencies);
    
    FILE *fp = fopen("tests/fixtures/test_bookmarks.csv", "r");
    assert_non_null(fp);
    
    LoadFrequencies(fp);
    fclose(fp);
    
    assert_int_equal(Frequencies_Max, 6);
    assert_int_equal(Frequencies[0].freq, 430037000);
    assert_string_equal(Frequencies[0].descr, " Beigua                   ");
    
    FreeFrequencies();
}

static void test_load_frequencies_empty_file(void **state)
{
    (void) state;
    
    Frequencies = calloc(FREQ_MAX, sizeof(FREQ));
    assert_non_null(Frequencies);
    
    FILE *fp = tmpfile();
    assert_non_null(fp);
    
    Frequencies_Max = 0;
    LoadFrequencies(fp);
    fclose(fp);
    
    assert_int_equal(Frequencies_Max, 0);
    
    FreeFrequencies();
}

static void test_frequency_tags_parsing(void **state)
{
    (void) state;
    
    Frequencies = calloc(FREQ_MAX, sizeof(FREQ));
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
    
    FreeFrequencies();
}

static void test_frequency_field_parsing(void **state)
{
    (void) state;
    
    Frequencies = calloc(FREQ_MAX, sizeof(FREQ));
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
    
    FreeFrequencies();
}

/* ========================================================================
 * Utility Function Tests (print_freq, ParseTags, etc.)
 * ======================================================================== */

static void test_print_freq_ghz_range(void **state)
{
    (void) state;
    
    /* Test GHz range formatting */
    freq_t freq = 1234567890000ULL; /* 1234.567.890 GHz */
    char *result = print_freq(freq, false);
    
    assert_non_null(result);
    assert_string_equal(result, "1234.567.890 GHz");
}

static void test_print_freq_mhz_range(void **state)
{
    (void) state;
    
    /* Test MHz range formatting */
    freq_t freq = 145000000; /* 145.000 MHz */
    char *result = print_freq(freq, false);
    
    assert_non_null(result);
    assert_string_equal(result, "145.000 MHz");
    
    /* Test another MHz value */
    freq = 430037000; /* 430.037 MHz */
    result = print_freq(freq, false);
    assert_string_equal(result, "430.037 MHz");
}

static void test_print_freq_khz_range(void **state)
{
    (void) state;
    
    /* Test KHz range formatting */
    freq_t freq = 10000; /* 10 KHz */
    char *result = print_freq(freq, false);
    
    assert_non_null(result);
    assert_string_equal(result, "10 KHz");
}

static void test_print_freq_rounding(void **state)
{
    (void) state;
    
    /* Test rounding to nearest KHz */
    freq_t freq = 145000500; /* Should round to 145.001 MHz */
    char *result = print_freq(freq, false);
    
    assert_non_null(result);
    assert_string_equal(result, "145.001 MHz");
}

static void test_parse_tags_single(void **state)
{
    (void) state;
    
    ResetOptTags();
    
    char tags[] = "VHF";
    bool result = ParseTags(tags);
    
    assert_true(result);
    assert_int_equal(opt_tag_max, 1);
    assert_string_equal(opt_tags[0], "VHF");
    
    ResetOptTags();
}

static void test_parse_tags_multiple(void **state)
{
    (void) state;
    
    ResetOptTags();
    
    char tags[] = "DMR|VHF|UHF";
    bool result = ParseTags(tags);
    
    assert_true(result);
    assert_int_equal(opt_tag_max, 3);
    assert_string_equal(opt_tags[0], "DMR");
    assert_string_equal(opt_tags[1], "VHF");
    assert_string_equal(opt_tags[2], "UHF");
    
    ResetOptTags();
}

static void test_parse_tags_empty(void **state)
{
    (void) state;
    
    ResetOptTags();
    
    char tags[] = "";
    bool result = ParseTags(tags);
    
    assert_false(result);
    assert_int_equal(opt_tag_max, 0);
}

static void test_filter_frequency_single_tag_match(void **state)
{
    (void) state;
    ResetOptTags();
    Frequencies = calloc(FREQ_MAX, sizeof(FREQ));
    assert_non_null(Frequencies);
    FILE *fp = fopen("tests/fixtures/test_bookmarks.csv", "r");
    assert_non_null(fp);
    LoadFrequencies(fp);
    fclose(fp);
    opt_tag_search = true;
    opt_tags[0] = strdup("DMR");
    opt_tag_max = 1;
    assert_true(FilterFrequency(0) != 0);
    assert_true(FilterFrequency(1) != 0);
    assert_true(FilterFrequency(2) != 0);
    assert_true(FilterFrequency(3) != 0);
    assert_int_equal(FilterFrequency(4), 0);
    assert_int_equal(FilterFrequency(5), 0);
    ResetOptTags();
    FreeFrequencies();
}

static void test_filter_frequency_multi_tag_or_match(void **state)
{
    (void) state;
    ResetOptTags();
    Frequencies = calloc(FREQ_MAX, sizeof(FREQ));
    assert_non_null(Frequencies);
    FILE *fp = fopen("tests/fixtures/test_bookmarks.csv", "r");
    assert_non_null(fp);
    LoadFrequencies(fp);
    fclose(fp);
    opt_tag_search = true;
    opt_tags[0] = strdup("DMR");
    opt_tags[1] = strdup("VHF");
    opt_tag_max = 2;
    for (int i = 0; i < 6; i++)
        assert_true(FilterFrequency(i) != 0);
    ResetOptTags();
    opt_tag_search = false;
    FreeFrequencies();
}

static void test_filter_frequency_no_tag_search_disabled(void **state)
{
    (void) state;
    Frequencies = calloc(FREQ_MAX, sizeof(FREQ));
    assert_non_null(Frequencies);
    FILE *fp = fopen("tests/fixtures/test_bookmarks.csv", "r");
    assert_non_null(fp);
    LoadFrequencies(fp);
    fclose(fp);
    opt_tag_search = false;
    assert_int_equal(FilterFrequency(0), 430037000);
    assert_int_equal(FilterFrequency(4), 144500000);
    FreeFrequencies();
}

static void test_filter_frequency_case_insensitive(void **state)
{
    (void) state;
    ResetOptTags();
    Frequencies = calloc(FREQ_MAX, sizeof(FREQ));
    assert_non_null(Frequencies);
    FILE *fp = fopen("tests/fixtures/test_bookmarks.csv", "r");
    assert_non_null(fp);
    LoadFrequencies(fp);
    fclose(fp);
    opt_tag_search = true;
    opt_tags[0] = strdup("dmr");
    opt_tag_max = 1;
    assert_true(FilterFrequency(1) != 0);
    ResetOptTags();
    opt_tag_search = false;
    FreeFrequencies();
}

static void test_filter_frequency_partial_match(void **state)
{
    (void) state;
    ResetOptTags();
    Frequencies = calloc(FREQ_MAX, sizeof(FREQ));
    assert_non_null(Frequencies);
    FILE *fp = fopen("tests/fixtures/test_bookmarks.csv", "r");
    assert_non_null(fp);
    LoadFrequencies(fp);
    fclose(fp);
    opt_tag_search = true;
    opt_tags[0] = strdup("Radio");
    opt_tag_max = 1;
    assert_true(FilterFrequency(3) != 0);
    assert_int_equal(FilterFrequency(0), 0);
    ResetOptTags();
    opt_tag_search = false;
    FreeFrequencies();
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

#ifdef HAVE_WRAP_SOCKET_MOCKS

/* ========================================================================
 * Protocol Tests (socket mocking via __wrap_write / __wrap_read)
 * ======================================================================== */

static void test_send(void **state)
{
    (void) state;
    mock_socket_reset();

    char cmd[] = "f\n";
    bool result = Send(MOCK_SOCKFD, cmd);

    assert_true(result);
    assert_string_equal(mock_socket_get_last_command(), "f\n");
}

static void test_recv(void **state)
{
    (void) state;
    mock_socket_reset();
    mock_socket_set_response("145000000\n");

    char buf[BUFSIZE] = {0};
    bool result = Recv(MOCK_SOCKFD, buf);

    assert_true(result);
    assert_string_equal(buf, "145000000\n");
}

static void test_get_current_freq_success(void **state)
{
    (void) state;
    mock_socket_reset();
    mock_socket_set_response("145000000\n");

    freq_t freq = 0;
    bool result = GetCurrentFreq(MOCK_SOCKFD, &freq);

    assert_true(result);
    assert_int_equal(freq, 145000000);
    assert_string_equal(mock_socket_get_last_command(), "f\n");
}

static void test_get_current_freq_rprt1(void **state)
{
    (void) state;
    mock_socket_reset();
    mock_socket_set_response("RPRT 1\n");

    freq_t freq = 0;
    bool result = GetCurrentFreq(MOCK_SOCKFD, &freq);

    assert_false(result);
}

static void test_get_signal_level_success(void **state)
{
    (void) state;
    mock_socket_reset();
    mock_socket_set_response("-12.5\n");

    double dBFS = 0.0;
    bool result = GetSignalLevel(MOCK_SOCKFD, &dBFS);

    assert_true(result);
    assert_true(dBFS > -12.51 && dBFS < -12.49);
    assert_string_equal(mock_socket_get_last_command(), "l\n");
}

static void test_get_signal_level_zero(void **state)
{
    (void) state;
    mock_socket_reset();
    mock_socket_set_response("0.0\n");

    double dBFS = -10.0;
    bool result = GetSignalLevel(MOCK_SOCKFD, &dBFS);

    assert_false(result);
}

static void test_get_signal_level_rprt1(void **state)
{
    (void) state;
    mock_socket_reset();
    mock_socket_set_response("RPRT 1\n");

    double dBFS = 0.0;
    bool result = GetSignalLevel(MOCK_SOCKFD, &dBFS);

    assert_false(result);
}

static void test_get_squelch_level(void **state)
{
    (void) state;
    mock_socket_reset();
    mock_socket_set_response("-80.0\n");

    double dBFS = 0.0;
    bool result = GetSquelchLevel(MOCK_SOCKFD, &dBFS);

    assert_true(result);
    assert_true(dBFS > -80.01 && dBFS < -79.99);
    assert_string_equal(mock_socket_get_last_command(), "l SQL\n");
}

static void test_set_squelch_level(void **state)
{
    (void) state;
    mock_socket_reset();
    mock_socket_set_response("RPRT 0\n");

    bool result = SetSquelchLevel(MOCK_SOCKFD, -80.0);

    assert_true(result);
}

static void test_set_squelch_level_rprt1(void **state)
{
    (void) state;
    mock_socket_reset();
    mock_socket_set_response("RPRT 1\n");

    bool result = SetSquelchLevel(MOCK_SOCKFD, -80.0);

    assert_false(result);
}

static void test_start_recording(void **state)
{
    (void) state;
    mock_socket_reset();
    mock_socket_set_response("RPRT 0\n");

    bool result = StartRecording(MOCK_SOCKFD);

    assert_true(result);
    assert_string_equal(mock_socket_get_last_command(), "U RECORD 1\n");
}

static void test_stop_recording(void **state)
{
    (void) state;
    mock_socket_reset();
    mock_socket_set_response("RPRT 0\n");

    bool result = StopRecording(MOCK_SOCKFD);

    assert_true(result);
    assert_string_equal(mock_socket_get_last_command(), "U RECORD 0\n");
}

static void test_set_freq_success(void **state)
{
    (void) state;
    mock_socket_reset();
    /* SetFreq: send "F %llu\n", Recv (RPRT check), then loop GetCurrentFreq */
    mock_socket_set_response("RPRT 0\n");
    mock_socket_add_response("145000000\n");

    bool result = SetFreq(MOCK_SOCKFD, 145000000);

    assert_true(result);
    assert_string_equal(mock_socket_get_last_command(), "f\n");
}

static void test_set_freq_retry_limit(void **state)
{
    (void) state;
    mock_socket_reset();
    /* Simulate Gqrx accepting the freq set command but GetCurrentFreq
       keeps returning RPRT 1 — should give up after 10 retries */
    mock_socket_set_response("RPRT 0\n");
    for (int i = 0; i < 10; i++)
        mock_socket_add_response("RPRT 1\n");

    bool result = SetFreq(MOCK_SOCKFD, 145000000);

    assert_false(result);
}

static void test_connect_localhost(void **state)
{
    (void) state;
    mock_socket_reset();

    int fd = Connect("localhost", 7356);

    assert_int_equal(fd, MOCK_SOCKFD);
    assert_string_equal(mock_socket_get_actual_host(), "localhost");
    assert_int_equal(mock_socket_get_actual_port(), 7356);
}

static void test_connect_custom_host_port(void **state)
{
    (void) state;
    mock_socket_reset();

    int fd = Connect("myremote", 9999);

    assert_int_equal(fd, MOCK_SOCKFD);
    assert_string_equal(mock_socket_get_actual_host(), "myremote");
    assert_int_equal(mock_socket_get_actual_port(), 9999);
}

static void test_debounce_signal_persists(void **state)
{
    (void)state;
    mock_socket_reset();

    /* GetSignalLevelEx ×5 with level -50.0 dBFS (above squelch -80) */
    mock_socket_set_response("-50.0\n");
    mock_socket_add_response("-50.0\n");
    mock_socket_add_response("-50.0\n");
    mock_socket_add_response("-50.0\n");
    mock_socket_add_response("-50.0\n");
    /* GetSquelchLevel returns -80.0 dBFS */
    mock_socket_add_response("-80.0\n");

    g_sockfd = MOCK_SOCKFD;
    assert_true(Debounce(145000000, -50.0));
}

static void test_debounce_signal_lost(void **state)
{
    (void)state;
    mock_socket_reset();

    /* GetSignalLevelEx ×5 with level -120.0 dBFS (below squelch -80) */
    mock_socket_set_response("-120.0\n");
    mock_socket_add_response("-120.0\n");
    mock_socket_add_response("-120.0\n");
    mock_socket_add_response("-120.0\n");
    mock_socket_add_response("-120.0\n");
    /* GetSquelchLevel returns -80.0 dBFS */
    mock_socket_add_response("-80.0\n");

    g_sockfd = MOCK_SOCKFD;
    assert_false(Debounce(145000000, -120.0));
}

static void test_backtrack_frequency_wraparound(void **state)
{
    (void)state;
    mock_socket_reset();

    /* current_freq=144050000, freq_interval=100000, numberOfIntervals=2,
       freq_min=144000000, freq_max=147000000

       i=0: current_freq = 143950000 < freq_min → wrap to 146900000
            GetSquelchLevel → -80.0
            SetFreq(146900000) → RPRT 0, 146900000
            GetSignalLevelEx ×5 → -50.0 each (above squelch → break)
       Return 146900000
    */
    /* Each GetSignalLevelEx calibrate call consumes 1 initial + up to 10 polls = 11 responses.
       Both probes read -50.0 (above squelch -80.0). */
    mock_socket_set_response("-80.0\n");
    mock_socket_add_response("RPRT 0\n");
    mock_socket_add_response("146900000\n");
    mock_socket_add_response("-50.0\n");
    mock_socket_add_response("-50.0\n");
    mock_socket_add_response("-50.0\n");
    mock_socket_add_response("-50.0\n");
    mock_socket_add_response("-50.0\n");
    mock_socket_add_response("-50.0\n");
    mock_socket_add_response("-50.0\n");
    mock_socket_add_response("-50.0\n");
    mock_socket_add_response("-50.0\n");
    mock_socket_add_response("-50.0\n");
    mock_socket_add_response("-50.0\n");

    mock_socket_add_response("-80.0\n");
    mock_socket_add_response("RPRT 0\n");
    mock_socket_add_response("146800000\n");
    mock_socket_add_response("-50.0\n");
    mock_socket_add_response("-50.0\n");
    mock_socket_add_response("-50.0\n");
    mock_socket_add_response("-50.0\n");
    mock_socket_add_response("-50.0\n");
    mock_socket_add_response("-50.0\n");
    mock_socket_add_response("-50.0\n");
    mock_socket_add_response("-50.0\n");
    mock_socket_add_response("-50.0\n");
    mock_socket_add_response("-50.0\n");
    mock_socket_add_response("-50.0\n");

    g_sockfd = MOCK_SOCKFD;
    freq_t bt_freq;
    double bt_level;
    bool found = BacktrackFrequency(144050000,
                                    100000, 2,
                                    144000000, 147000000,
                                    &bt_freq, &bt_level);
    assert_true(found);
    assert_int_equal(bt_freq, 146900000);
}

static void test_backtrack_frequency_not_found(void **state)
{
    (void)state;
    mock_socket_reset();

    /* current_freq=145000000, freq_interval=100000, numberOfIntervals=2,
       freq_min=144000000, freq_max=147000000

       i=0: current_freq=144900000, SetFreq(144900000)
            GetSignalLevelEx → -120.0 (below squelch -80 → continue)
       i=1: current_freq=144800000, SetFreq(144800000)
            GetSignalLevelEx → -120.0 (below squelch → continue, loop ends)
       Return 144800000
    */
    mock_socket_set_response("-80.0\n");
    mock_socket_add_response("RPRT 0\n");
    mock_socket_add_response("144900000\n");
    mock_socket_add_response("-120.0\n");
    mock_socket_add_response("-120.0\n");
    mock_socket_add_response("-120.0\n");
    mock_socket_add_response("-120.0\n");
    mock_socket_add_response("-120.0\n");

    mock_socket_add_response("-80.0\n");
    mock_socket_add_response("RPRT 0\n");
    mock_socket_add_response("144800000\n");
    mock_socket_add_response("-120.0\n");
    mock_socket_add_response("-120.0\n");
    mock_socket_add_response("-120.0\n");
    mock_socket_add_response("-120.0\n");
    mock_socket_add_response("-120.0\n");

    g_sockfd = MOCK_SOCKFD;
    freq_t bt_freq;
    double bt_level;
    bool found = BacktrackFrequency(145000000,
                                    100000, 2,
                                    144000000, 147000000,
                                    &bt_freq, &bt_level);
    assert_false(found);
    assert_int_equal(bt_freq, 144800000);
}

#endif /* HAVE_WRAP_SOCKET_MOCKS */

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

        /* Tag filter tests */
        cmocka_unit_test(test_filter_frequency_single_tag_match),
        cmocka_unit_test(test_filter_frequency_multi_tag_or_match),
        cmocka_unit_test(test_filter_frequency_no_tag_search_disabled),
        cmocka_unit_test(test_filter_frequency_case_insensitive),
        cmocka_unit_test(test_filter_frequency_partial_match),

#ifdef HAVE_WRAP_SOCKET_MOCKS
        /* Protocol tests */
        cmocka_unit_test(test_send),
        cmocka_unit_test(test_recv),
        cmocka_unit_test(test_get_current_freq_success),
        cmocka_unit_test(test_get_current_freq_rprt1),
        cmocka_unit_test(test_get_signal_level_success),
        cmocka_unit_test(test_get_signal_level_zero),
        cmocka_unit_test(test_get_signal_level_rprt1),
        cmocka_unit_test(test_get_squelch_level),
        cmocka_unit_test(test_set_squelch_level),
        cmocka_unit_test(test_set_squelch_level_rprt1),
        cmocka_unit_test(test_start_recording),
        cmocka_unit_test(test_stop_recording),
        cmocka_unit_test(test_set_freq_success),
        cmocka_unit_test(test_set_freq_retry_limit),

        /* Scan function tests */
        cmocka_unit_test(test_debounce_signal_persists),
        cmocka_unit_test(test_debounce_signal_lost),
        cmocka_unit_test(test_backtrack_frequency_wraparound),
        cmocka_unit_test(test_backtrack_frequency_not_found),

        /* Connect tests */
        cmocka_unit_test(test_connect_localhost),
        cmocka_unit_test(test_connect_custom_host_port),
#endif
    };
    
    return cmocka_run_group_tests(tests, NULL, NULL);
}
