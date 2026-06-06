#ifndef VOX_LPC_H
#define VOX_LPC_H

//
// VoxLpcAutocorr — Linear Predictive Coding analysis
//
// Modular, reusable functions for LPC analysis used by the G.729B VAD.
// Each function is independent so calls can be added or removed as needed.
//
// The overall LPC chain is:
//   VoxLpcAutocorr(buf, N, order, r)
//   VoxLpcLagWindow(r, order)
//   VoxLpcLevinson(r, order, a, rc) → returns residual energy
//   VoxLpcLsf(a, order, lsf)
//   VoxLpcLowBandEnergy(r, order, N)  or  VoxLpcFullBandEnergy(r, N)
//

//
// Autocorrelation: r[k] = Σ_{n=0}^{N-1-k} x[n] * x[n+k],  k = 0 .. order
//   x       — input samples (s16)
//   N       — number of samples in x
//   order   — LPC order (10 for G.729)
//   r       — output array of size (order + 1); r[0] is the zero-lag energy
//
// Uses double internally — called once per VAD frame (~10 ms), negligible cost.
//
void VoxLpcAutocorr(const short *x, int N, int order, double *r);

//
// VoxLpcLagWindow — apply 60 Hz bandwidth expansion to autocorrelation
//   r_g729[k] = r[k] * exp(-½ (π * 60 / 4000 * k)²)
//
// This smooths the LPC spectrum and prevents sharp resonances that would
// make LSFs sensitive to noise.  Standard G.729 pre-processing.
//
void VoxLpcLagWindow(double *r, int order);

//
// VoxLpcLevinson — Levinson-Durbin recursion
//   r[0..order]  — autocorrelation
//   a[0..order]  — output LPC coefficients, a[0] = 1.0
//   rc[1..order] — output reflection coefficients (rc[0] unused)
//   returns      — prediction residual energy (E = r[0] * Π(1 - rc[k]²))
//
double VoxLpcLevinson(const double *r, int order, double *a, double *rc);

//
// VoxLpcLsf — compute line spectral frequencies from LPC coefficients
//   a[0..order]   — LPC coefficients (a[0] = 1.0)
//   order         — LPC order (10 for G.729)
//   lsf[0..order-1] — output LSFs, normalized to [0.0, 0.5]
//                     (i.e. fraction of 8000 Hz sampling rate)
//
void VoxLpcLsf(const double *a, int order, double *lsf);

//
// VoxLpcFullBandEnergy — full-band energy in dB
//   r[0] — zero-lag autocorrelation
//   N    — number of samples in the frame
//   Returns 10 * log10(r[0] / N)
//
double VoxLpcFullBandEnergy(double r0, int N);

//
// VoxLpcLowBandEnergy — low-band energy from weighted autocorrelation
//   r[0..order] — autocorrelation
//   order       — LPC order
//   N           — number of samples in the frame
//   Returns 10 * log10(Elow / N)
//
// The weighting applies a 2 kHz low-pass filter in the autocorrelation
// domain using the G.729B LBF_CORR coefficients (filtered at 1 kHz
// cutoff, derived from a 12-tap FIR).
//
double VoxLpcLowBandEnergy(const double *r, int order, int N);

//
// VoxLpcZeroCrossings — zero-crossing rate normalized by frame length
//   x        — input samples
//   N        — number of samples
//   Returns  ZC_rate = (Σ sign_changes) / N,  range [0.0, 1.0]
//
double VoxLpcZeroCrossings(const short *x, int N);

//
// Return |rc[1]| (absolute value of first reflection coefficient).
// Used by the VAD to detect spectral tilt (hiss → flat → small).
//
double VoxLpcRc1(const double *rc);

//
// VoxLpcPredictionGain — LPC prediction gain in dB
//   r0       — zero-lag autocorrelation r[0]
//   residual — residual energy from VoxLpcLevinson()
//   Returns 10 * log10(r0 / residual)
//
double VoxLpcPredictionGain(double r0, double residual);

//
// VoxLpcPitchStrength — maximum normalized autocorrelation at pitch lags
//   x        — input samples (s16)
//   N        — number of samples
//   Returns max normalized autocorrelation r[lag]/r[0] for
//   lag ∈ [min_lag, max_lag], or 0 if N is too small.
//
// Pitch lags at 8 kHz: lag 20 = 400 Hz, lag 160 = 50 Hz.
// Voice produces strong peaks (0.3-0.8). Band-limited noise < 0.2.
// Gain-invariant (ratio scales equally with amplitude).
//
double VoxLpcPitchStrength(const short *x, int N, int min_lag, int max_lag);

#endif
