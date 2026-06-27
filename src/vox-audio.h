#ifndef VOX_AUDIO_H
#define VOX_AUDIO_H

//
// VOX / Audio Activity Detection
//
// This module captures raw s16 mono 8 kHz audio from the PipeWire null-sink
// monitor created by gqrx-scan-setup-audio.sh.  It uses a G.729B-style
// voice activity detector (VAD) that combines:
//
//   - Autocorrelation-based LPC analysis (vox-lpc.c)
//   - Multi-boundary VAD decision with adaptive noise floor tracking (vox-vad.c)
//   - Energy statistics (avg_dev, energy, lag-1 autocorrelation)
//
// The VAD distinguishes voice from stationary receiver hiss by evaluating
// differential energy, spectral distortion, zero-crossing rate, and
// energy non-stationarity.
//
// External dependency:
//   gqrx-scan-setup-audio.sh attach <process> must have been run first.
//   That script creates a null sink named "gqrx-scanner-intercept" and
//   routes the target application's audio through it.  This module reads
//   the monitor of that sink.
//
// Calling sequence:
//   VoxAudioInit()      once at startup
//   VoxAudioShutdown()  once at teardown
//
// Query functions — called from the scan loop on every iteration when the
// carrier is present.  VoxAudioInit() must have returned true first.
//
// VoxAudioHasSignal() reads a small chunk of raw s16 PCM from the capture
// pipe, computes LPC features, and runs the G.729B VAD state machine.
//
// VoxAudioIsAlive() returns false if the capture pipe has closed (pw-cat
// died or disconnected).
//

#include <stdio.h>
#include <stdbool.h>
#include "vox-lpc.h"
#include "vox-vad.h"

//
// Energy detection constants — declared in vox-audio.c, readable by tests.
//
extern const long g_vox_threshold;
extern const long g_vox_sample_bytes;

//
// VoxAudioInit
//   Open the pw-cat capture pipe from the Gqrx null-sink monitor.
//   Ignores SIGPIPE so that a crashed/disconnected pw-cat does not kill us.
//   Returns true on success, false on error (pw-cat not found, Gqrx not
//   streaming, etc.).
//
bool VoxAudioInit(void);

//
// VoxAudioShutdown
//   Close the capture pipe, reap pw-cat, and restore the original SIGPIPE
//   disposition.  Safe to call even if VoxAudioInit was never called or
//   failed (no-op when already shut down).
//
void VoxAudioShutdown(void);

//
// VoxAudioFlush
//   Read and discard any data already buffered in the capture pipe.
//   Returns true if the pipe is still alive, false on EOF/error.
//   Internal — invoked by VoxAudioReset(); not typically called directly.
//
bool VoxAudioFlush(void);

//
// VoxAudioReset
//   Prepare the audio capture and VAD state for detection on a new
//   frequency.  Flushes stale audio from the pipe and resets the VAD
//   state machine (hangover, pitch-sustain counters) so the previous
//   frequency does not contaminate the next measurement.
//   Call once when entering a new frequency, before the listen loop.
//
void VoxAudioReset(void);

//
// VoxAudioStats
//   Statistics computed from a buffer of raw s16 PCM samples.
//
typedef struct {
    long mean;      // sample mean (DC offset)
    long avg_dev;   // average absolute deviation from mean  (amplitude)
    long energy;    // Σ (s - μ)²                            (AC power)
    long corr;      // Σ (s[n-1] - μ)(s[n] - μ)              (lag-1 autocovariance)
    int  nsamples;  // number of samples analyzed
} VoxAudioStats;

//
// VoxAudioComputeStats(buf, nsamples, stats)
//   Two-pass statistics over a raw s16 buffer.  Pass 1 computes the
//   sample mean (μ).  Pass 2 computes avg_dev (|s-μ|), energy ((s-μ)²),
//   and corr ((s[n-1]-μ)(s[n]-μ)).  Mean subtraction ensures that DC
//   offset does not masquerade as adjacent-sample correlation.
//
void VoxAudioComputeStats(const short *buf, int nsamples, VoxAudioStats *stats);

//
// VoxAudioPrintStats(buf, nsamples, threshold)
//   Compute stats, then print a one-line debug summary to stderr
//   including hex dump (first 32 bytes), avg_dev, energy, corr‰,
//   mean, threshold, and SIGNAL/silence verdict.
//
void VoxAudioPrintStats(const short *buf, int nsamples, long threshold);

//
// VoxAudioHexDump(buf, nsamples)
//   Print hex dump of first 32 bytes (or fewer if nsamples shorter)
//   to stderr.  Useful for debugging signal characteristics without
//   pulling in the full stats computation.
//
void VoxAudioHexDump(const short *buf, int nsamples);

//
// VoxAudioHasSignal(sample_time_us)
//   Poll up to sample_time_us microseconds for audio data, read a chunk,
//   compute LPC features (autocorrelation, reflection coeffs, LSF,
//   zero-crossing rate, energies), and run the G.729B VAD state machine.
//
//   Falls back to the simple avg_dev > threshold comparison when the
//   VAD state has not yet converged (first 128 frames).
//
//   Returns true if voice activity is detected.
//   Returns false for silence, timeout, or if the pipe is not open
//   (caller should check VoxAudioIsAlive() to disambiguate).
//
bool VoxAudioHasSignal(long sample_time_us);

//
// VoxAudioIsAlive
//   Return true if the capture pipe is still open and pw-cat is running.
//   Returns false if the pipe was never opened, or if ferror/feof has
//   been detected.
//
bool VoxAudioIsAlive(void);

//
// VoxAudioRestart
//   One-shot restart of the pw-cat capture pipe after it dies (e.g. pipe
//   closed, pw-cat crashed, or detach cleanup killed it).  Closes the old
//   pipe and spawns a new pw-cat.  The VAD state machine is re-initialised
//   so stale noise estimates do not carry over.
//
//   Returns true if the new pw-cat started successfully.
//   Returns false on failure — caller should disable VOX permanently.
//
//   Safe to call only after a successful VoxAudioInit().  Not thread-safe.
//
bool VoxAudioRestart(void);

#endif
