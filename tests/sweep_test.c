#include "../gqrx-scan.h"
#include "mocks/mock_socket.h"
#include "sweep_parser.h"

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

/* Diagnostic output (goes to stderr — not captured by the stdout pipe) */
#define diag(...)  fprintf(stderr, __VA_ARGS__)

/* ==================================================================
 * validate_hit_line — verify the stdout line for a hit
 *
 * Searches for "Freq: XXX.XXX MHz" matching the given frequency in the
 * captured output and validates the line format:
 *   - "active" present
 *   - descr format: non-NULL → "active [descr],", NULL → "active,"
 *   - "Level: -XX.XX/-XX.XX" format
 * ================================================================== */

static void validate_hit_line(const char *captured, freq_t freq,
                              const char *descr)
{
    freq_t rounded = (freq_t)(round(freq / 1000.0) * 1000.0);
    long mhz = (rounded / 1000000) % 1000;
    long khz = (rounded / 1000) % 1000;

    char freq_str[64];
    snprintf(freq_str, sizeof(freq_str), "Freq: %ld.%3.3ld MHz", mhz, khz);

    const char *pos = strstr(captured, freq_str);
    if (!pos)
    {
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "Expected hit line '%s' not found in stdout",
                 freq_str);
        fail_msg("%s", msg);
        return;
    }

    const char *line_end = strstr(pos + 1, "Freq: ");
    if (!line_end)
        line_end = pos + strlen(pos);

    size_t line_len = (size_t)(line_end - pos);
    char line[2048];
    size_t copy_len = line_len < sizeof(line) - 1 ? line_len : sizeof(line) - 1;
    strncpy(line, pos, copy_len);
    line[copy_len] = '\0';

    if (!strstr(line, "active"))
    {
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "Hit line for freq %llu missing 'active': '%.200s'",
                 (unsigned long long)freq, line);
        fail_msg("%s", msg);
        return;
    }

    if (descr && descr[0] != '\0')
    {
        char expected[256];
        snprintf(expected, sizeof(expected), "active [%s],", descr);
        if (!strstr(line, expected))
        {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "Hit line for freq %llu missing descr '%s': '%.200s'",
                     (unsigned long long)freq, descr, line);
            fail_msg("%s", msg);
            return;
        }
    }
    else
    {
        if (!strstr(line, "active,"))
        {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "Hit line for freq %llu missing 'active,': '%.200s'",
                     (unsigned long long)freq, line);
            fail_msg("%s", msg);
            return;
        }
    }

    const char *level_tag = strstr(line, "Level: ");
    if (!level_tag)
    {
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "Hit line for freq %llu missing 'Level:': '%.200s'",
                 (unsigned long long)freq, line);
        fail_msg("%s", msg);
        return;
    }

    const char *lv = level_tag + 7;
    if (*lv != '-' || !strchr(lv, '/'))
    {
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "Hit line for freq %llu has malformed Level: '%.200s'",
                 (unsigned long long)freq, line);
        fail_msg("%s", msg);
    }
}

/* ==================================================================
 * Run a sweep test from a profile file
 * ================================================================== */

static void run_sweep_test(const char *profile_path)
{
    SetOptDefaults();

    mock_socket_reset();

    assert_true(mock_load_profile(profile_path));
    if (mock_profile_min_freq() == 0 || mock_profile_max_freq() == 0)
    {
        diag("FAIL: profile missing MIN_FREQ or MAX_FREQ (required)\n");
        fail_msg("profile missing MIN_FREQ or MAX_FREQ (required)");
    }
    opt_min_freq = mock_profile_min_freq();
    opt_max_freq = mock_profile_max_freq();

    int sockfd = Connect("localhost", 7356);
    assert_int_equal(sockfd, MOCK_SOCKFD);

#ifdef TESTING_BUILD
    g_testing_max_full_sweeps = 2;
#endif

    int pipefd[2];
    assert_int_equal(pipe(pipefd), 0);
    int old_stdout = dup(STDOUT_FILENO);
    assert_int_equal(dup2(pipefd[1], STDOUT_FILENO), STDOUT_FILENO);
    close(pipefd[1]);

    ScanFrequenciesInRange(sockfd, opt_min_freq, opt_max_freq,
                           opt_scan_bw);
    fflush(stdout);

    dup2(old_stdout, STDOUT_FILENO);
    close(old_stdout);

    char captured[65536];
    ssize_t n = read(pipefd[0], captured, sizeof(captured) - 1);
    close(pipefd[0]);
    captured[n > 0 ? n : 0] = '\0';

    freq_t hit_freqs[SWEEP_PARSER_MAX_HITS];
    int n_hits = parse_hit_report(captured, hit_freqs,
                                   SWEEP_PARSER_MAX_HITS);
    int nh = mock_expected_count();

    diag("  Expected: ");
    for (int e = 0; e < nh; e++)
        diag("%llu±%llu ", (unsigned long long)mock_expected_freq(e),
             (unsigned long long)mock_expected_tolerance(e));
    diag(" Found: ");
    for (int s = 0; s < SavedFreq_Max && s < nh; s++)
    {
        freq_t ef = mock_expected_freq(s);
        freq_t sf = SavedFrequencies[s].freq;
        long long delta = (long long)sf - (long long)ef;
        diag("%llu (%+lld) ", (unsigned long long)sf, delta);
    }
    diag("\n");

    assert_int_equal(SavedFreq_Max, nh);

    for (int e = 0; e < nh; e++)
    {
        freq_t ef = mock_expected_freq(e);
        freq_t tol = mock_expected_tolerance(e);
        bool found = false;

        for (int s = 0; s < SavedFreq_Max; s++)
        {
            freq_t diff = (SavedFrequencies[s].freq > ef)
                        ? SavedFrequencies[s].freq - ef
                        : ef - SavedFrequencies[s].freq;
            if (diff <= tol)
            {
                found = true;
                break;
            }
        }

        if (!found)
        {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "Expected hit %llu ±%llu not found in SavedFrequencies",
                     (unsigned long long)ef, (unsigned long long)tol);
            msg[sizeof(msg) - 1] = '\0';
            fail_msg("%s", msg);
        }
    }

    for (int e = 0; e < nh; e++)
    {
        freq_t ef = mock_expected_freq(e);
        freq_t tol = mock_expected_tolerance(e);
        bool found = false;

        for (int h = 0; h < n_hits; h++)
        {
            freq_t diff = (hit_freqs[h] > ef)
                        ? hit_freqs[h] - ef
                        : ef - hit_freqs[h];
            if (diff <= tol)
            {
                found = true;
                break;
            }
        }

        if (!found)
        {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "Expected hit %llu ±%llu not found in stdout",
                     (unsigned long long)ef, (unsigned long long)tol);
            msg[sizeof(msg) - 1] = '\0';
            fail_msg("%s", msg);
        }

        validate_hit_line(captured, ef, NULL);
    }

    mock_socket_reset();
#ifdef TESTING_BUILD
    g_testing_max_full_sweeps = -1;
#endif
}

/* ==================================================================
 * Run a bookmark test from a profile + manually-populated bookmarks
 * ================================================================== */

static void run_bookmark_test(const char *profile_path,
                              const freq_t *bookmark_freqs, int n_bookmarks,
                              const char **bookmark_descrs,
                              int n_loops)
{
    SetOptDefaults();
    ResetOptTags();
    FreeFrequencies();

    mock_socket_reset();

    assert_true(mock_load_profile(profile_path));
    if (mock_profile_min_freq() == 0 || mock_profile_max_freq() == 0)
        fail_msg("profile missing MIN_FREQ or MAX_FREQ (required)");
    opt_min_freq = mock_profile_min_freq();
    opt_max_freq = mock_profile_max_freq();

    Frequencies = calloc(FREQ_MAX, sizeof(FREQ));
    assert_non_null(Frequencies);
    for (int i = 0; i < n_bookmarks; i++)
    {
        Frequencies[i].freq = bookmark_freqs[i];
        if (bookmark_descrs && bookmark_descrs[i])
            strncpy(Frequencies[i].descr, bookmark_descrs[i], BUFSIZE - 1);
    }
    Frequencies_Max = n_bookmarks;

    int sockfd = Connect("localhost", 7356);
    assert_int_equal(sockfd, MOCK_SOCKFD);

#ifdef TESTING_BUILD
    g_testing_max_bookmark_loops = n_loops;
#endif

    int pipefd[2];
    assert_int_equal(pipe(pipefd), 0);
    int old_stdout = dup(STDOUT_FILENO);
    assert_int_equal(dup2(pipefd[1], STDOUT_FILENO), STDOUT_FILENO);
    close(pipefd[1]);

    ScanBookmarkedFrequenciesInRange(sockfd, opt_min_freq, opt_max_freq);
    fflush(stdout);

    dup2(old_stdout, STDOUT_FILENO);
    close(old_stdout);

    char captured[65536];
    ssize_t n = read(pipefd[0], captured, sizeof(captured) - 1);
    close(pipefd[0]);
    captured[n > 0 ? n : 0] = '\0';

    freq_t hit_freqs[SWEEP_PARSER_MAX_HITS];
    int n_hits = parse_hit_report(captured, hit_freqs,
                                   SWEEP_PARSER_MAX_HITS);
    int nh = mock_expected_count();

    /* Helper to look up a bookmark's descr by frequency */
    const char *lookup(freq_t f) {
        for (int b = 0; b < n_bookmarks; b++)
            if (bookmark_freqs[b] == f)
                return bookmark_descrs ? bookmark_descrs[b] : NULL;
        return NULL;
    }

    diag("  Expected: ");
    for (int e = 0; e < nh; e++) {
        freq_t ef = mock_expected_freq(e);
        const char *d = lookup(ef);
        diag("%llu", (unsigned long long)ef);
        if (d) diag(" (%s)", d);
        diag("±%llu ", (unsigned long long)mock_expected_tolerance(e));
    }
    diag(" Found (%d hits): ", n_hits);
    for (int h = 0; h < n_hits; h++) {
        const char *d = lookup(hit_freqs[h]);
        diag("%llu", (unsigned long long)hit_freqs[h]);
        if (d) diag(" (%s)", d);
        diag(" ");
    }
    diag("\n");

    for (int e = 0; e < nh; e++)
    {
        freq_t ef = mock_expected_freq(e);
        freq_t tol = mock_expected_tolerance(e);
        bool found = false;

        for (int h = 0; h < n_hits; h++)
        {
            freq_t diff = (hit_freqs[h] > ef)
                        ? hit_freqs[h] - ef
                        : ef - hit_freqs[h];
            if (diff <= tol)
            {
                found = true;
                break;
            }
        }

        if (!found)
        {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "Expected hit %llu ±%llu not found in stdout",
                     (unsigned long long)ef, (unsigned long long)tol);
            fail_msg("%s", msg);
        }

        const char *d = NULL;
        for (int b = 0; b < n_bookmarks; b++)
        {
            if (bookmark_freqs[b] == ef)
            {
                d = bookmark_descrs ? bookmark_descrs[b] : NULL;
                break;
            }
        }
        validate_hit_line(captured, ef, d);
    }

    FreeFrequencies();
    mock_socket_reset();
#ifdef TESTING_BUILD
    g_testing_max_bookmark_loops = -1;
#endif
}

/* ==================================================================
 * Sweep test cases
 * ================================================================== */

static void test_sweep_noise_only(void **state)
{
    (void)state;
    run_sweep_test("tests/profiles/noise_only.txt");
}

static void test_sweep_two_peaks(void **state)
{
    (void)state;
    run_sweep_test("tests/profiles/two_peaks.txt");
}

static void test_sweep_nearby_signals(void **state)
{
    (void)state;
    run_sweep_test("tests/profiles/two_nearby_signals.txt");
}

static void test_sweep_three_peaks(void **state)
{
    (void)state;
    run_sweep_test("tests/profiles/three_peaks.txt");
}

/* ==================================================================
 * Bookmark test cases
 * ================================================================== */

static void test_bookmark_all_hit(void **state)
{
    (void)state;

    freq_t bookmarks[] = { 145000000ULL, 146000000ULL };
    const char *descrs[] = { "Repeater A", "Repeater B" };
    int n = sizeof(bookmarks) / sizeof(bookmarks[0]);

    run_bookmark_test("tests/profiles/bookmark_all_hit.txt",
                      bookmarks, n, descrs, 1);
}

static void test_bookmark_some_hit(void **state)
{
    (void)state;

    freq_t bookmarks[] = { 145000000ULL, 145500000ULL, 146000000ULL };
    const char *descrs[] = { "Repeater A", "Noise", "Silent" };
    int n = sizeof(bookmarks) / sizeof(bookmarks[0]);

    run_bookmark_test("tests/profiles/bookmark_some_hit.txt",
                      bookmarks, n, descrs, 1);
}

static void test_bookmark_all_noise(void **state)
{
    (void)state;

    freq_t bookmarks[] = { 145000000ULL, 146000000ULL };
    const char *descrs[] = { "Silent A", "Silent B" };
    int n = sizeof(bookmarks) / sizeof(bookmarks[0]);

    run_bookmark_test("tests/profiles/bookmark_all_noise.txt",
                      bookmarks, n, descrs, 1);
}

/* ==================================================================
 * Test runner
 * ================================================================== */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_sweep_noise_only),
        cmocka_unit_test(test_sweep_two_peaks),
        cmocka_unit_test(test_sweep_nearby_signals),
        cmocka_unit_test(test_sweep_three_peaks),
        cmocka_unit_test(test_bookmark_all_hit),
        cmocka_unit_test(test_bookmark_some_hit),
        cmocka_unit_test(test_bookmark_all_noise),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
