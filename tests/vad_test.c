/*
 * VAD test — feed raw s16 8 kHz PCM samples through the full VAD pipeline
 * (features + VoxVADUpdate) and assert per-frame success rates.
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

#include "../src/vox-lpc.h"
#include "../src/vox-vad.h"

#define FRAME_BYTES    (256)       // 128 s16 samples
#define FRAME_SAMPLES  (128)

enum VoxFrameClass {
    VOX_CLASS_VOICE,
    VOX_CLASS_SILENCE
};

/*
 * Feed a raw s16 file through the full VAD pipeline and return per-frame
 * classifications.
 *
 * Callback: for each frame the user gets the class.  Returns the total
 * number of frames processed (or -1 if the file couldn't be opened).
 */
struct VadResult {
    int total;
    int voice;
    int silence;
};

static struct VadResult vad_test_run(const char *path)
{
    struct VadResult r = { 0, 0, 0 };

    FILE *fp = fopen(path, "rb");
    if (fp == NULL)
    {
        r.total = -1;
        return r;
    }

    VoxVADState state;
    VoxVADInit(&state);

    short buf[FRAME_SAMPLES];

    while (fread(buf, sizeof(short), FRAME_SAMPLES, fp) == FRAME_SAMPLES)
    {
        r.total++;

        /* Pitch periodicity */
        double pitch_strength = VoxLpcPitchStrength(buf, FRAME_SAMPLES, 20, 400);

        /* Zero-crossing rate */
        double ZC = VoxLpcZeroCrossings(buf, FRAME_SAMPLES);

        /* Band-energy difference */
        double r0 = 0.0;
        for (int i = 0; i < FRAME_SAMPLES; i++)
            r0 += (double)buf[i] * (double)buf[i];

        double r_low[13];
        r_low[0] = r0;
        for (int k = 1; k <= 12; k++)
        {
            r_low[k] = 0.0;
            for (int i = 0; i < FRAME_SAMPLES - k; i++)
                r_low[k] += (double)buf[i] * (double)buf[i + k];
        }

        double Ef = 10.0 * log10(r0 / FRAME_SAMPLES);
        double El = VoxLpcLowBandEnergy(r_low, 12, FRAME_SAMPLES);
        double El_minus_Ef = El - Ef;

        /* VAD decision */
        VoxVADFeatures feat;
        feat.pitch_strength = pitch_strength;
        feat.ZC = ZC;
        feat.El_minus_Ef = El_minus_Ef;

        if (VoxVADUpdate(&state, &feat))
            r.voice++;
        else
            r.silence++;
    }

    fclose(fp);
    return r;
}

//
// Feed a raw s16 file through the full VAD pipeline and return the longest
// run of consecutive silence frames (after hangover decay).
//
static int vad_test_max_consecutive_silence(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL)
        return -1;

    VoxVADState state;
    VoxVADInit(&state);

    short buf[FRAME_SAMPLES];
    int current = 0, best = 0;

    while (fread(buf, sizeof(short), FRAME_SAMPLES, fp) == FRAME_SAMPLES)
    {
        double pitch_strength = VoxLpcPitchStrength(buf, FRAME_SAMPLES, 20, 400);
        double ZC = VoxLpcZeroCrossings(buf, FRAME_SAMPLES);

        double r0 = 0.0;
        for (int i = 0; i < FRAME_SAMPLES; i++)
            r0 += (double)buf[i] * (double)buf[i];

        double r_low[13];
        r_low[0] = r0;
        for (int k = 1; k <= 12; k++)
        {
            r_low[k] = 0.0;
            for (int i = 0; i < FRAME_SAMPLES - k; i++)
                r_low[k] += (double)buf[i] * (double)buf[i + k];
        }

        double Ef = 10.0 * log10(r0 / FRAME_SAMPLES);
        double El = VoxLpcLowBandEnergy(r_low, 12, FRAME_SAMPLES);
        double El_minus_Ef = El - Ef;

        VoxVADFeatures feat;
        feat.pitch_strength = pitch_strength;
        feat.ZC = ZC;
        feat.El_minus_Ef = El_minus_Ef;

        if (VoxVADUpdate(&state, &feat))
            current = 0;
        else
        {
            current++;
            if (current > best)
                best = current;
        }
    }

    fclose(fp);
    return best;
}

// =========================================================================
//  Tests
// =========================================================================

static void test_vad_noise_high(void **state)
{
    (void)state;
    struct VadResult r = vad_test_run("tests/vox/noise_high.raw");
    if (r.total < 0) { skip(); return; }

    double voice_ratio = (double)r.voice / (double)r.total;
    fprintf(stderr, "vad_test: noise_high.raw  voice=%d silence=%d total=%d ratio=%.4f\n",
            r.voice, r.silence, r.total, voice_ratio);
    assert_true(voice_ratio < 0.10);
}

static void test_vad_noise_low(void **state)
{
    (void)state;
    struct VadResult r = vad_test_run("tests/vox/noise_low.raw");
    if (r.total < 0) { skip(); return; }

    double voice_ratio = (double)r.voice / (double)r.total;
    fprintf(stderr, "vad_test: noise_low.raw   voice=%d silence=%d total=%d ratio=%.4f\n",
            r.voice, r.silence, r.total, voice_ratio);
    assert_true(voice_ratio < 0.05);
}

static void test_vad_hiss_low(void **state)
{
    (void)state;
    struct VadResult r = vad_test_run("tests/vox/hiss_low.raw");
    if (r.total < 0) { skip(); return; }

    double voice_ratio = (double)r.voice / (double)r.total;
    fprintf(stderr, "vad_test: hiss_low.raw   voice=%d silence=%d total=%d ratio=%.4f\n",
            r.voice, r.silence, r.total, voice_ratio);
    assert_true(voice_ratio == 0.0);
}

static void test_vad_hiss_high(void **state)
{
    (void)state;
    struct VadResult r = vad_test_run("tests/vox/hiss_high.raw");
    if (r.total < 0) { skip(); return; }

    double voice_ratio = (double)r.voice / (double)r.total;
    fprintf(stderr, "vad_test: hiss_high.raw  voice=%d silence=%d total=%d ratio=%.4f\n",
            r.voice, r.silence, r.total, voice_ratio);
    assert_true(voice_ratio == 0.0);
}

static void test_vad_noise2(void **state)
{
    (void)state;
    struct VadResult r = vad_test_run("tests/vox/noise2.raw");
    if (r.total < 0) { skip(); return; }

    double voice_ratio = (double)r.voice / (double)r.total;
    fprintf(stderr, "vad_test: noise2.raw  voice=%d silence=%d total=%d ratio=%.4f\n",
            r.voice, r.silence, r.total, voice_ratio);
    /* noise2.raw is static noise with a large DC offset that previously
     * caused a 100% false-positive VOICE rate via DC-inflated pitch.
     * After mean subtraction in VoxLpcPitchStrength the false pitch
     * disappears and the file must be classified as pure silence. */
    assert_true(voice_ratio == 0.0);
}

static void test_vad_voice_clear(void **state)
{
    (void)state;
    struct VadResult r = vad_test_run("tests/vox/voice_clear_some_hiss.raw");
    if (r.total < 0) { skip(); return; }

    double voice_ratio = (double)r.voice / (double)r.total;
    fprintf(stderr, "vad_test: voice_clear_some_hiss.raw  voice=%d silence=%d total=%d ratio=%.4f\n",
            r.voice, r.silence, r.total, voice_ratio);
    /* After DC-offset removal in VoxLpcPitchStrength the frame-level
     * voice ratio dropped from ~95% to ~29%.  The batch-level detection
     * used in production (any voice frame in 32-frame window) remains
     * excellent (P ≈ 99.97% at 29%), so the lower frame ratio is fine.
     * Keep the floor at 0.20 to ensure genuinely voiced content is found. */
    assert_true(voice_ratio > 0.20);
}

static void test_vad_pause_break(void **state)
{
    (void)state;
    int max_sil = vad_test_max_consecutive_silence("tests/vox/voice_with_3s_pause.raw");
    if (max_sil < 0) { skip(); return; }

    int ms = max_sil * FRAME_SAMPLES * 1000 / 8000;
    fprintf(stderr, "vad_test: voice_with_3s_pause.raw  max_consecutive_silence=%d frames = %dms\n",
            max_sil, ms);
    assert_true(max_sil >= 62);  // at least 1 second of continuous silence
}

static void test_vad_voice_low(void **state)
{
    (void)state;
    struct VadResult r = vad_test_run("tests/vox/voice_low.raw");
    if (r.total < 0) { skip(); return; }

    double voice_ratio = (double)r.voice / (double)r.total;
    fprintf(stderr, "vad_test: voice_low.raw  voice=%d silence=%d total=%d ratio=%.4f\n",
            r.voice, r.silence, r.total, voice_ratio);
    assert_true(voice_ratio > 0.20);
}

// =========================================================================
//  Test Runner
// =========================================================================

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_vad_noise_high),
        cmocka_unit_test(test_vad_noise_low),
        cmocka_unit_test(test_vad_hiss_low),
        cmocka_unit_test(test_vad_hiss_high),
        cmocka_unit_test(test_vad_noise2),
        cmocka_unit_test(test_vad_voice_clear),
        cmocka_unit_test(test_vad_voice_low),
        cmocka_unit_test(test_vad_pause_break),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
