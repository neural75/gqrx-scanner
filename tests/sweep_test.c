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

/* Diagnostic output (goes to stderr — not captured by the stdout pipe) */
#define diag(...)  fprintf(stderr, __VA_ARGS__)

/* ==================================================================
 * Run a sweep test from a profile file
 * ================================================================== */

static void run_sweep_test(const char *profile_path)
{
    /* Reset globals */
    SetOptDefaults();

    /* Allocate a minimal Frequencies array for sweep
     * ScanFrequenciesInRange expects Frequencies[i].noise_floor */
    if (!Frequencies)
        Frequencies = calloc(FREQ_MAX, sizeof(FREQ));
    assert_non_null(Frequencies);
    Frequencies_Max = 0;

    mock_socket_reset();

    /* Load the profile (must declare MIN_FREQ/MAX_FREQ) */
    assert_true(mock_load_profile(profile_path));
    if (mock_profile_min_freq() == 0 || mock_profile_max_freq() == 0)
    {
        diag("FAIL: profile missing MIN_FREQ or MAX_FREQ (required)\n");
        fail_msg("profile missing MIN_FREQ or MAX_FREQ (required)");
    }
    opt_min_freq = mock_profile_min_freq();
    opt_max_freq = mock_profile_max_freq();

    /* Connect */
    int sockfd = Connect("localhost", 7356);
    assert_int_equal(sockfd, MOCK_SOCKFD);

    /* WaitUserInputOrDelay exits after 1 loop iteration via opt_max_listen. */

    /* Limit to 2 full sweeps */
#ifdef TESTING_BUILD
    g_testing_max_full_sweeps = 2;
#endif

    /* Capture stdout */
    int pipefd[2];
    assert_int_equal(pipe(pipefd), 0);
    int old_stdout = dup(STDOUT_FILENO);
    assert_int_equal(dup2(pipefd[1], STDOUT_FILENO), STDOUT_FILENO);
    close(pipefd[1]);

    /* Run the sweep */
    ScanFrequenciesInRange(sockfd, opt_min_freq, opt_max_freq,
                           opt_scan_bw, 0.0);
    fflush(stdout);

    /* Restore stdout */
    dup2(old_stdout, STDOUT_FILENO);
    close(old_stdout);

    /* Read captured output */
    char captured[65536];
    ssize_t n = read(pipefd[0], captured, sizeof(captured) - 1);
    close(pipefd[0]);
    captured[n > 0 ? n : 0] = '\0';

    /* Parse hit lines from stdout */
    freq_t hit_freqs[SWEEP_PARSER_MAX_HITS];
    int n_hits = parse_hit_report(captured, hit_freqs, SWEEP_PARSER_MAX_HITS);
    int nh = mock_expected_count();

    /* Diagnostic: show expected vs found frequencies with delta */
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

    /* Check SavedFrequencies count matches expected */
    assert_int_equal(SavedFreq_Max, nh);

    /* Check each expected hit is found in SavedFrequencies within tolerance */
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

    /* Verify parsed stdout hits — each expected freq found as hit line */
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
    }

    /* Clean up */
    mock_socket_reset();
#ifdef TESTING_BUILD
    g_testing_max_full_sweeps = -1;
#endif
}

/* ==================================================================
 * Test cases — one per profile
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
    run_sweep_test("tests/profiles/two_nearby_signal.txt");
}

static void test_sweep_three_peaks(void **state)
{
    (void)state;
    run_sweep_test("tests/profiles/three_peaks.txt");
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
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
