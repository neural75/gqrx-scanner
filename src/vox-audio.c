#include "vox-audio.h"
#include "vox-lpc.h"
#include "vox-vad.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <math.h>

//
// Temporary — opt_verbose is declared in gqrx-scan.h, but including that
// header here would create a circular dependency (gqrx-scan.h includes
// vox-audio.h).  Forward-declare it for debug logging only.
//
extern bool opt_verbose;

//
// VOX energy detection constants
//
// The capture pipe delivers raw s16 PCM at 8 kHz (two bytes per sample,
// little-endian, signed).  Silence is at sample value 0.  We compute the
// average absolute deviation from 0 over each poll window and compare it
// to a threshold.
//
//   Open squelch / receiver hiss  → avg deviation ~100-500
//   Quiet voice / distant signal  → avg deviation ~300-600
//   Loud/nearby voice             → avg deviation ~1000+
//
// g_vox_threshold of 500 separates voice from noise-floor hiss.
// g_vox_sample_bytes is the number of bytes to read per poll (must be a
// multiple of sizeof(short) = 2).
//
const long g_vox_threshold    = 500;
const long g_vox_sample_bytes = 256;  // 128 samples @ 2 bytes each

//
// Static state — this module manages a single capture stream.
// There is only one audio sink to monitor (the Gqrx null-sink), so one
// pipe is sufficient.
//
static FILE *audio_fp = NULL;
static int   audio_fd  = -1;
static bool  pipe_dead = false;

//
// Saved SIGPIPE handler so we can restore it on shutdown.
// popen() gives us a pipe to a child process.  If the child crashes
// or disconnects while we are trying to fread(), the kernel delivers
// SIGPIPE to us.  The default action for SIGPIPE is to terminate the
// process — which would kill the scanner.  We ignore it instead so that
// fread() simply returns 0 / EOF and we can detect the loss of audio
// gracefully.
//
static struct sigaction old_sa;

//
// G.729B VAD state instance — one per process.
// Initialised on the first call to VoxAudioHasSignal().
//
static VoxVADState g_vad_state;
static bool g_vad_inited = false;

// ---------------------------------------------------------------------------
// Primary implementation — PipeWire (pw-cat)
//
// Dependency: pipewire-bin (pw-cat, pw-cli, pw-link, pw-loopback)
//   sudo apt install pipewire-bin
//
// The monitor source "gqrx-scanner-intercept.monitor" must exist before
// calling this — run gqrx-scan-setup-audio.sh attach first.
// ---------------------------------------------------------------------------

bool VoxAudioInit(void)
{
    struct sigaction sa;
    int rc;

    // Idempotent — if the pipe is already open there is nothing to do.
    if (audio_fp != NULL)
        return true;

    // ------ SIGPIPE handling ------
    //
    // 1. Query current SIGPIPE disposition (don't assume SIG_DFL).
    // 2. Override with SIG_IGN so that the child dying does not take us
    //    down.
    // 3. Save the old handler for restoration in VoxAudioShutdown().
    //
    // The query can fail (e.g. EINVAL), but that is non-fatal — we log
    // and continue; the sigaction that follows will still work because
    // old_sa is filled with whatever the query wrote (undefined on error,
    // but SIG_IGN is still safer than the default).
    if (sigaction(SIGPIPE, NULL, &old_sa) == -1)
        fprintf(stderr, "[ WARNING ] Could not query SIGPIPE handler: %s\n",
                strerror(errno));

    sa = old_sa;
    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &sa, &old_sa) == -1)
        fprintf(stderr, "[ WARNING ] Could not ignore SIGPIPE: %s\n",
                strerror(errno));

    // ------ Probe for pw-cat ------
    //
    // We use "command -v pw-cat" (POSIX) instead of "which" because
    // "which" is not guaranteed to be present on every system.  The
    // exit status tells us whether pw-cat is reachable in $PATH.
    //
    // We check separately rather than letting the real popen fail
    // because popen() itself succeeds as long as fork() works — the
    // shell will just print "not found" to stderr and exit 127.  By
    // probing first we can print a specific, helpful error message.
    //
    FILE *probe = popen("command -v pw-cat >/dev/null 2>&1", "r");
    if (probe == NULL)
    {
        fprintf(stderr, "[ ERROR ] Failed to probe for pw-cat: %s\n",
                strerror(errno));
        goto fail_sig;
    }
    rc = pclose(probe);
    if (rc != 0)
    {
        fprintf(stderr, "[ ERROR ] pw-cat not found in PATH.\n"
                "        pipewire-bin package is required.\n");
        goto fail_sig;
    }

    // ------ Open capture pipe ------
    //
    // Command: pw-cat --record --properties="..." --channels=1 --format=u8
    //   --rate=8000 --raw -
    //
    // --properties="stream.capture.sink=true target.object=gqrx-scanner-intercept"
    //   Tells PipeWire to capture from the sink's monitor output ports
    //   rather than looking for an Audio/Source node.  The "target.object"
    //   identifies the sink by node.name.  This is the same SPA property
    //   syntax used by pw-loopback's -i flag.
    //
    //   Without this flag, pw-cat --record only sees nodes with
    //   media.class=Audio/Source.  The null sink's monitor is exposed as
    //   output ports on the sink node itself, not as a separate Source
    //   node, so stream.capture.sink is required.
    //
    // - (dash filename)
    //   Write raw PCM to stdout so popen can read it.
    //
    // --channels=1 --format=u8 --rate=8000 --raw
    //   Raw unsigned 8-bit PCM at 8 kHz, mono.  This is good enough for
    //   voice-activity detection and keeps the data rate low (~8 kB/s).
    //   pw-cat decodes from PipeWire's internal format into u8 for us.
    //
    // 2>/dev/null
    //   Suppress pw-cat's own error messages (e.g. "Connection refused"
    //   when PipeWire is not running).  We detect failure via fread()
    //   returning 0/EOF instead.
    //
    audio_fp = popen("pw-cat --record "
                     "--properties=\"stream.capture.sink=true "
                     "target.object=gqrx-scanner-intercept\" "
                     "--channels=1 --format=s16 --rate=8000 "
                     "--raw - 2>/dev/null", "r");
    if (audio_fp == NULL)
    {
        fprintf(stderr, "[ ERROR ] Failed to start audio capture: %s\n"
                "         Make sure to run 'gqrx-scan-setup-audio.sh attach <app>' before starting gqrx-scanner.'\n",
                strerror(errno));
        goto fail_sig;
    }

    // Unbuffer stdio so fileno() stays in sync with the kernel pipe fd.
    setvbuf(audio_fp, NULL, _IONBF, 0);

    // Get the raw fd for poll() / read(), and make it non-blocking so
    // that our timed poll()+read() loop never blocks.
    audio_fd = fileno(audio_fp);
    fcntl(audio_fd, F_SETFL, fcntl(audio_fd, F_GETFL) | O_NONBLOCK);
    pipe_dead = false;

    return true;

    // ------ Error exit ------
    //
    // If anything went wrong we must undo the SIGPIPE override before
    // returning, otherwise the caller could later crash on a SIGPIPE
    // that they were not expecting.
fail_sig:
    sigaction(SIGPIPE, &old_sa, NULL);
    return false;
}

void VoxAudioShutdown(void)
{
    int rc;

    // Guard against double-shutdown or shutdown-without-init.
    if (audio_fp == NULL)
        return;

    // Close the pipe and reap the child process.
    //
    // pclose() waits for the child to exit and returns its exit status.
    // A non-zero exit is not necessarily an error — pw-cat exits with
    // code 0 when it receives SIGTERM, but may exit non-zero if it
    // encountered a problem (e.g. monitor source disappeared).  We log
    // a warning in any case so that surprising failures are visible.
    audio_fd = -1;
    pipe_dead = false;
    rc = pclose(audio_fp);
    audio_fp = NULL;

    if (rc != 0)
    {
        fprintf(stderr, "[ WARNING ] Audio capture exited unexpectedly");
        if (WIFEXITED(rc))
            fprintf(stderr, " (status %d)", WEXITSTATUS(rc));
        if (WIFSIGNALED(rc))
            fprintf(stderr, " (signal %d)", WTERMSIG(rc));
        fprintf(stderr, ". "
                "Check that PipeWire is running and "
                "gqrx-scan-setup-audio.sh is attached.\n");
    }

    // Restore SIGPIPE to whatever it was before we changed it.
    // This is critical for politeness — other code in the process may
    // rely on the default SIGPIPE behaviour.
    sigaction(SIGPIPE, &old_sa, NULL);
}

//
// VoxAudioIsAlive
//   Return true if the capture pipe is open and pw-cat is still running.
//   The pipe_dead flag is set to true when VoxAudioHasSignal() detects
//   EOF (read() returning 0).  This lets the caller distinguish between
//   "silence" and "pipe died" when VoxAudioHasSignal() returns false.
//
bool VoxAudioIsAlive(void)
{
    return audio_fp != NULL && !pipe_dead;
}

//
// VoxAudioFlush (internal)
//   Read and discard any data already buffered in the pipe.  poll() with
//   zero timeout, then read() until EAGAIN.  Sets pipe_dead on EOF.
//   Returns true if the pipe is still alive, false on EOF/error.
//
bool VoxAudioFlush(void)
{
    // Use the same-size buffer as VoxAudioHasSignal for efficiency.
    short buf[128];
    struct pollfd pfd = { .fd = audio_fd, .events = POLLIN };

    while (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN))
    {
        ssize_t r = read(audio_fd, buf, sizeof(buf));
        if (r <= 0)
        {
            if (r == 0)
                pipe_dead = true;
            return false;
        }
    }
    return !pipe_dead;
}

//
// VoxAudioComputeStats(buf, nsamples, stats)
//   Two-pass statistics over a raw s16 buffer.  See vox-audio.h for
//   the struct and field semantics.
//
//   Pass 1: compute sample mean (μ) — the DC offset.
//   Pass 2: compute avg_dev (|s-μ|), energy ((s-μ)²), and
//           corr ((s[n-1]-μ)(s[n]-μ)).
//
//   Mean subtraction is critical: raw s16 samples often have a non-zero
//   DC component.  Without it, corr/energy ≈ 1 regardless of the actual
//   adjacent-sample correlation, because DC² dominates the numerator
//   and denominator equally.
//
void VoxAudioComputeStats(const short *buf, int nsamples, VoxAudioStats *stats)
{
    long sum = 0;

    // Pass 1: sample mean
    for (int i = 0; i < nsamples; i++)
        sum += (long)buf[i];
    long mean = sum / nsamples;
    long sum_dev = 0, corr = 0, energy = 0;

    // Pass 2: de-meaned stats
    for (int i = 0; i < nsamples; i++)
    {
        long s = (long)buf[i] - mean;
        if (s < 0)
            sum_dev += -s;
        else
            sum_dev += s;
        energy += s * s;
        if (i > 0)
        {
            long sp = (long)buf[i - 1] - mean;
            corr += sp * s;
        }
    }

    stats->mean     = mean;
    stats->avg_dev  = sum_dev / nsamples;
    stats->energy   = energy;
    stats->corr     = corr;
    stats->nsamples = nsamples;
}

//
// VoxAudioHexDump(buf, nsamples)
//   Print hex dump of up to 32 bytes to stderr.
//
void VoxAudioHexDump(const short *buf, int nsamples)
{
    int show = (nsamples * 2) < 32 ? (nsamples * 2) : 32;
    fprintf(stderr, "vox: hex=");
    const unsigned char *bytes = (const unsigned char *)buf;
    for (int i = 0; i < show; i++)
        fprintf(stderr, "%02x%c", bytes[i],
                i + 1 < show ? ' ' : '\n');
}

//
// VoxAudioPrintStats(buf, nsamples, threshold)
//   Compute stats via VoxAudioComputeStats(), then print a debug line
//   to stderr with hex dump (first 32 bytes) and the full stats summary.
//
void VoxAudioPrintStats(const short *buf, int nsamples, long threshold)
{
    VoxAudioStats stats;

    VoxAudioComputeStats(buf, nsamples, &stats);

    VoxAudioHexDump(buf, nsamples);

    // corr/energy is the lag-1 autocorrelation coefficient (ACF-1).
    // noise  → near 0 (uncorrelated), voice  → 300–900‰
    long corr_permille = stats.energy > 0
                         ? stats.corr * 1000 / stats.energy
                         : 0;
    fprintf(stderr, "vox: avg_dev=%ld corr=%ld energy=%ld corr‰=%ld"
            " mean=%ld threshold=%ld -> %s\n",
            stats.avg_dev, stats.corr, stats.energy,
            corr_permille, stats.mean,
            threshold,
            stats.avg_dev > threshold ? "SIGNAL" : "silence");
}

//
// VoxAudioHasSignal(sample_time_us)
//   1. Guard: if the pipe is closed or known dead, return false.
//   2. Sample: poll() with caller's timeout, read one buffer chunk
//      (256 bytes = 128 s16 samples).
//   3. Compute amplitude-invariant features:
//        pitch strength (max norm autocorr at 50-400 Hz lags)
//        zero-crossing rate
//        band-energy difference (low-band minus full-band)
//   4. Run VoxVADUpdate() — pitch-based 4-criteria VAD.
//
//   The caller must call VoxAudioFlush() once before entering the
//   detection loop for a new frequency, so stale data does not pollute
//   the measurement.
//
bool VoxAudioHasSignal(long sample_time_us)
{
    short buf[128];   // 128 s16 samples = 256 bytes
    struct pollfd pfd = { .fd = audio_fd, .events = POLLIN };

    // ------ Initialise VAD state once ------
    if (!g_vad_inited)
    {
        VoxVADInit(&g_vad_state);
        g_vad_inited = true;
    }

    // ------ Guard ------
    if (audio_fp == NULL || pipe_dead)
        return false;

    // ------ Wait for data ------
    {
        int pr = poll(&pfd, 1, sample_time_us / 1000);
        if (opt_verbose)
            fprintf(stderr, "vox: poll ret=%d revents=0x%x (timeout=%ldms)\n",
                    pr, pfd.revents, sample_time_us / 1000);
        if (pr <= 0)
        {
            if (opt_verbose)
                fprintf(stderr, pr == 0 ? "vox: fresh poll timeout\n"
                         : "vox: fresh poll error (errno=%d)\n", errno);
            return false;
        }
    }

    // ------ Read fresh chunk ------
    ssize_t n = read(audio_fd, buf, sizeof(buf));
    if (opt_verbose)
        fprintf(stderr, "vox: read %zd bytes\n", n);
    if (n <= 0)
    {
        if (opt_verbose)
            fprintf(stderr, "vox: read EOF/error (pipe_dead=%d)\n", n == 0);
        if (n == 0)
            pipe_dead = true;
        return false;
    }

    int nsamples = (int)(n / (ssize_t)sizeof(short));
    if (nsamples < 2)
        return false;

    // ------ Compute features ------

    // A. Basic energy stats (avg_dev, etc.)
    VoxAudioStats stats;
    VoxAudioComputeStats(buf, nsamples, &stats);

    // B. Pitch periodicity (strongest indicator)
    double pitch_strength = VoxLpcPitchStrength(buf, nsamples, 20, 160);

    // C. Zero-crossing rate
    double ZC = VoxLpcZeroCrossings(buf, nsamples);

    // D. Band-energy difference (El - Ef) — gain-invariant
    double r0 = 0.0;
    for (int i = 0; i < nsamples; i++)
        r0 += (double)buf[i] * (double)buf[i];

    double r_low[13];
    r_low[0] = r0;
    for (int k = 1; k <= 12; k++)
    {
        r_low[k] = 0.0;
        for (int i = 0; i < nsamples - k; i++)
            r_low[k] += (double)buf[i] * (double)buf[i + k];
    }

    double Ef = VoxLpcFullBandEnergy(r0, nsamples);
    double El = VoxLpcLowBandEnergy(r_low, 12, nsamples);
    double El_minus_Ef = El - Ef;

    // ------ Build feature vector & run VAD ------
    VoxVADFeatures feat;
    feat.pitch_strength = pitch_strength;
    feat.ZC = ZC;
    feat.El_minus_Ef = El_minus_Ef;

    bool vad_decision = VoxVADUpdate(&g_vad_state, &feat);

    // ------ Debug logging ------
    if (opt_verbose)
    {
        long corr_permille = stats.energy > 0
                             ? stats.corr * 1000 / stats.energy
                             : 0;
        int score = (pitch_strength > 0.30 ? 2 : 0)
                  + (ZC > 0.50 ? 1 : 0)
                  + (ZC < 0.12 ? 1 : 0)
                  + (El_minus_Ef > 4.0 ? 1 : 0);

        fprintf(stderr, "vox: dev=%ld pitch=%.3f ZC=%.3f El-Ef=%.1f"
                " score=%d frame=%d hang=%d -> %s\n",
                stats.avg_dev,
                pitch_strength, ZC, El_minus_Ef,
                score,
                g_vad_state.frame_count,
                g_vad_state.hangover,
                vad_decision ? "VOICE" : "silence");
        (void)corr_permille;
    }

    return vad_decision;
}

// ---------------------------------------------------------------------------
// Reference implementation — PulseAudio (pacat), kept for debug
//
// Alternative to the primary functions above.  Uses pacat instead of
// pw-cat, which requires pulseaudio-utils:
//   sudo apt install pulseaudio-utils
//
// Only difference from the primary version is the tool name, flags, and
// error messages.  The SIGPIPE / state management logic is identical.
// ---------------------------------------------------------------------------

bool VoxAudioInit_PA(void)
{
    struct sigaction sa;
    int rc;

    if (audio_fp != NULL)
        return true;

    if (sigaction(SIGPIPE, NULL, &old_sa) == -1)
        fprintf(stderr, "[ WARNING ] Could not query SIGPIPE handler: %s\n",
                strerror(errno));

    sa = old_sa;
    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &sa, &old_sa) == -1)
        fprintf(stderr, "[ WARNING ] Could not ignore SIGPIPE: %s\n",
                strerror(errno));

    FILE *probe = popen("command -v pacat >/dev/null 2>&1", "r");
    if (probe == NULL)
    {
        fprintf(stderr, "[ ERROR ] Failed to probe for pacat: %s\n",
                strerror(errno));
        goto fail_sig;
    }
    rc = pclose(probe);
    if (rc != 0)
    {
        fprintf(stderr, "[ ERROR ] pacat not found in PATH.\n"
                "         Install pulseaudio-utils:\n"
                "           sudo apt install pulseaudio-utils\n");
        goto fail_sig;
    }

    audio_fp = popen("pacat --record -d gqrx-scanner-intercept.monitor "
                     "--channels=1 --format=u8 --rate=8000 "
                     "--file-format=raw 2>/dev/null", "r");
    if (audio_fp == NULL)
    {
        fprintf(stderr, "[ ERROR ] Failed to start audio capture: %s\n"
                "         Make sure gqrx-scan-setup-audio.sh is running\n",
                strerror(errno));
        goto fail_sig;
    }

    return true;

fail_sig:
    sigaction(SIGPIPE, &old_sa, NULL);
    return false;
}

void VoxAudioShutdown_PA(void)
{
    int rc;

    if (audio_fp == NULL)
        return;

    rc = pclose(audio_fp);
    audio_fp = NULL;

    if (rc != 0)
    {
        fprintf(stderr, "[ WARNING ] Audio capture exited unexpectedly");
        if (WIFEXITED(rc))
            fprintf(stderr, " (status %d)", WEXITSTATUS(rc));
        if (WIFSIGNALED(rc))
            fprintf(stderr, " (signal %d)", WTERMSIG(rc));
        fprintf(stderr, ". "
                "Check that PulseAudio/PipeWire is running and "
                "gqrx-scan-setup-audio.sh is attached.\n");
    }

    sigaction(SIGPIPE, &old_sa, NULL);
}
