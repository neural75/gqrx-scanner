#include "vox-lpc.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

// =========================================================================
//  Constants
// =========================================================================

//
// G.729B lag-window coefficients for 60 Hz bandwidth expansion at 8 kHz.
//   w[k] = exp(-½ (π * 60 / 4000 * k)²)
//
// Computed as w[k] = exp(-(k * LAG_W_ALPHA)²)  where LAG_W_ALPHA = π*60/4000 * √½
//
#define LAG_W_ALPHA  (0.033289)

//
// LBF_CORR — low-band filter correlation coefficients (G.729B Table 2 / eq. 15)
//
// These coefficients define the impulse response of a low-pass filter
// (cutoff ~1 kHz at 8 kHz sampling) applied in the autocorrelation domain.
//
// Elow = Σ_{k=-12}^{12} LBF_CORR[|k|] * r[k]
//
// where r[k] is the autocorrelation with lag k, and k=0..order.
// Only non-negative k are listed; the filter is symmetric.
//
#define LBF_ORDER  (12)

static const double LBF_CORR[] = {
    /* 0 */  2.0 / 3.0,
    /* 1 */  0.571220,  0.421352,  0.285038,  0.158620,
    /* 5 */  0.067629,  0.020400,  0.004025,  0.0,
    /* 9 */  0.0,        0.0,        0.0,        0.0
};

//
// Minimum LPC residual energy — prevents division by zero / log(0)
//
#define LPC_E_MIN  (1e-30)
#define LSF_N_GRID (512)    // grid points for LSF root search
#define LSF_N_ITER (20)     // Newton iterations per interval

// =========================================================================
//  VoxLpcAutocorr
// =========================================================================

void VoxLpcAutocorr(const short *x, int N, int order, double *r)
{
    // Zero the output array
    for (int k = 0; k <= order; k++)
        r[k] = 0.0;

    // r[0] = Σ x[n]²
    for (int n = 0; n < N; n++)
    {
        double s = (double)x[n];
        r[0] += s * s;
    }

    // r[k] = Σ x[n] * x[n+k]
    for (int k = 1; k <= order; k++)
    {
        int kmax = N - k;
        for (int n = 0; n < kmax; n++)
            r[k] += (double)x[n] * (double)x[n + k];
    }
}

// =========================================================================
//  VoxLpcLagWindow
// =========================================================================

void VoxLpcLagWindow(double *r, int order)
{
    for (int k = 0; k <= order; k++)
        r[k] *= exp(-(k * LAG_W_ALPHA) * (k * LAG_W_ALPHA));
}

// =========================================================================
//  VoxLpcLevinson
// =========================================================================

double VoxLpcLevinson(const double *r, int order, double *a, double *rc)
{
    double e;           // current residual energy
    double a_prev[order + 1];

    // Guard against all-zero signal (silence / muted)
    if (r[0] < LPC_E_MIN)
    {
        for (int i = 0; i <= order; i++)
            a[i] = 0.0;
        a[0] = 1.0;
        for (int i = 1; i <= order; i++)
            rc[i] = 0.0;
        return LPC_E_MIN;
    }

    // Initialise for order 0
    a[0] = 1.0;
    e = r[0];

    for (int i = 1; i <= order; i++)
    {
        // Compute reflection coefficient rc[i]
        double acc = r[i];
        for (int j = 1; j < i; j++)
            acc += a[j] * r[i - j];

        rc[i] = -acc / e;

        // Save previous a[] before updating
        memcpy(a_prev, a, (size_t)(i) * sizeof(double));

        // Update a[j] = a[j] + rc[i] * a[i-j]
        a[0] = 1.0; // a[0] always stays 1.0
        for (int j = 1; j < i; j++)
            a[j] = a_prev[j] + rc[i] * a_prev[i - j];
        a[i] = rc[i];

        // Update residual energy
        e *= 1.0 - rc[i] * rc[i];
    }

    return e;
}

// =========================================================================
//  Internal — Chebyshev polynomial evaluation for LSF root search
// =========================================================================

//
// Evaluate the polynomial P(x) or Q(x) at a given cos(ω) value using
// Chebyshev series.  For a polynomial of degree m:
//   p(cos ω) = 2 * Σ f[n] * T_n(cos ω) - f[0]
//
// where f[n] are the first m+1 coefficients of the Chebyshev expansion,
// and T_n are Chebyshev polynomials of the first kind.
//
// Instead of computing the full Chebyshev series, we use the Clenshaw
// recurrence (stable and efficient for this use).
//
// Input:
//   f[0..m] — polynomial coefficients in Chebyshev form
//             f[0] = sum(poly), f[1..m] = adjacent differences
//   x       — cos(ω)
//   m       — degree
//
// Returns p(cos ω)
//
static double chebyshev_eval(const double *f, double x, int m)
{
    double b0, b1, b2;
    int i;

    b0 = f[0];
    b1 = 0.0;
    for (i = 1; i <= m; i++)
    {
        b2 = b1;
        b1 = b0;
        b0 = x * b1 - b2 + f[i];
    }
    return (b0 - b1) * 0.5;
}

//
// Convert LPC coefficients a[0..order] to the P(z) and Q(z) polynomials
// used for LSF computation.
//
// For even order M = order:
//   P(z) = A(z) + z^{-(M+1)} * A(z^{-1})    (symmetric)
//   Q(z) = A(z) - z^{-(M+1)} * A(z^{-1})    (anti-symmetric)
//
// P and Q are of degree M+1, with known roots at z = ±1.
//
// On output:
//   p_coeff[0..M]  — coefficients of P'(z), the reduced polynomial
//                    (after dividing out (z+1) since M+1 is odd)
//   q_coeff[0..M]  — coefficients of Q'(z)
//                    (after dividing out (z-1) since M+1 is odd)
//   np, nq         — degrees of P' and Q' (both = M)
//
static void lpc_to_pq(const double *a, int order,
                       double *p_coeff, int *np,
                       double *q_coeff, int *nq)
{
    int m = order;               // G.729 order = 10
    double p[m + 2];             // P degree M+1 = 11
    double q[m + 2];             // Q degree M+1 = 11
    int i;

    // P[i] = a[i] + a[M+1-i],  i=0..M+1, with a[M+1]=0
    p[0] = 1.0;
    for (i = 1; i <= m; i++)
        p[i] = a[i] + a[m + 1 - i];
    p[m + 1] = 1.0;          // a[M+1] = 0, a[0] = 1 → p[M+1]=1

    // Q[i] = a[i] - a[M+1-i],  i=0..M+1
    q[0] = 1.0;
    for (i = 1; i <= m; i++)
        q[i] = a[i] - a[m + 1 - i];
    q[m + 1] = -1.0;         // a[M+1]=0, a[0]=1 → q[M+1] = -1

    // Since M+1 = 11 is odd, P(z) has an explicit root at z = -1.
    // Q(z) has an explicit root at z = +1.
    // Remove them via synthetic division.
    //
    // P'(z) = P(z) / (z + 1)
    // Q'(z) = Q(z) / (z - 1)
    //
    // Both P' and Q' are of degree M.
    //
    // Synthetic division of a polynomial poly[0..M+1] by (z - r):
    //   result[k] = result[k-1] * r + poly[k],  result[-1] = 0
    //

    // P' divide by (z + 1) → r = -1
    p_coeff[0] = p[0];
    for (i = 1; i <= m; i++)
        p_coeff[i] = p_coeff[i - 1] * (-1.0) + p[i];
    *np = m;

    // Q' divide by (z - 1) → r = +1
    q_coeff[0] = q[0];
    for (i = 1; i <= m; i++)
        q_coeff[i] = q_coeff[i - 1] * 1.0 + q[i];
    *nq = m;
}

//
// Convert polynomial P'(x) or Q'(x) to Chebyshev series coefficients.
// For the Chebyshev evaluation, we need:
//   f[0] = Σ poly[i]
//   f[k] = sum of poly[i] where (i+k) is even pattern
//        = poly[k] - poly[k+2] + poly[k+4] - ...   for k >= 1
//
// The polynomial poly[0..deg] represents coefficients of x^i.
//
// Returns the number of Chebyshev coefficients (deg + 1).
//
static int poly_to_chebyshev(const double *poly, int deg, double *f)
{
    int i, k;
    int m = deg;

    // f[0] = Σ poly[i]
    f[0] = 0.0;
    for (i = 0; i <= m; i++)
        f[0] += poly[i];

    // f[k] = poly[k] - poly[k+2] + poly[k+4] - ...  (alternating sum)
    for (k = 1; k <= m; k++)
    {
        double sum = 0.0;
        for (i = k; i <= m; i += 2)
        {
            if (((i - k) / 2) % 2 == 0)
                sum += poly[i];
            else
                sum -= poly[i];
        }
        f[k] = sum;
    }
    return m;
}

// =========================================================================
//  VoxLpcLsf
// =========================================================================

void VoxLpcLsf(const double *a, int order, double *lsf)
{
    double p_coeff[order + 1];      // P' coefficients
    double q_coeff[order + 1];      // Q' coefficients
    int np, nq;
    int lsf_idx = 0;
    int i;

    lpc_to_pq(a, order, p_coeff, &np, q_coeff, &nq);

    // Convert P' and Q' to Chebyshev form
    double f_p[order + 1];
    double f_q[order + 1];
    poly_to_chebyshev(p_coeff, np, f_p);
    poly_to_chebyshev(q_coeff, nq, f_q);

    // Search for roots on the unit circle at N_GRID grid points in
    // ω = [0, π].  The LSFs are alternating roots of P'(cos ω) and
    // Q'(cos ω) for increasing ω (they interlace).
    //
    // We search for P'(x) then Q'(x) roots in alternating order.
    //
    double x0 = 1.0;                 // cos(0) = 1
    double f0_p = chebyshev_eval(f_p, x0, np);
    double f0_q = chebyshev_eval(f_q, x0, nq);

    // We need a sign change to find a root; start with whichever polynomial
    // has a non-zero value at x0.
    //
    int use_p = 1;                   // start with P
    double prev_x = x0;
    double prev_f = use_p ? f0_p : f0_q;

    for (int grid = 1; grid <= LSF_N_GRID; grid++)
    {
        double x = cos(M_PI * grid / LSF_N_GRID);
        double fp = chebyshev_eval(f_p, x, np);
        double fq = chebyshev_eval(f_q, x, nq);
        double f = use_p ? fp : fq;

        // Check for sign change
        if (prev_f * f < 0.0)
        {
            // Root bracketed between prev_x and x.
            // Refine with simple bisection (fast enough for LSF search).
            double x_low = prev_x;
            double x_high = x;
            double f_low = prev_f;
            double f_high = f;

            for (int iter = 0; iter < LSF_N_ITER; iter++)
            {
                double x_mid = (x_low + x_high) * 0.5;
                double f_mid = chebyshev_eval(
                    use_p ? f_p : f_q, x_mid, use_p ? np : nq);
                if (f_mid * f_low < 0.0)
                {
                    x_high = x_mid;
                    f_high = f_mid;
                }
                else
                {
                    x_low = x_mid;
                    f_low = f_mid;
                }
            }

            double x_root = (x_low + x_high) * 0.5;

            // Convert cos(ω) → ω / π  =  acos(x_root) / π
            // Result is in [0.0, 0.5] = [0, 4000 Hz] / 8000 Hz
            lsf[lsf_idx] = acos(x_root) / M_PI;
            lsf_idx++;

            // Alternate polynomial for next root
            use_p = !use_p;

            // If we found all 10 LSFs, stop
            if (lsf_idx >= order)
                break;

            // After alternating, evaluate the new function at (x + prev_x)/2
            // to avoid missing adjacent roots
            x0 = x;
            f0_p = fp;
            f0_q = fq;
        }

        prev_x = x;
        prev_f = f;
    }

    // If some LSFs weren't found (very unusual), fill remaining with
    // evenly spaced values — this prevents crashes but means the
    // spectral distortion metric will be bogus for that frame.
    for (i = lsf_idx; i < order; i++)
        lsf[i] = (double)(i + 1) / (double)(order + 1) * 0.5;
}

// =========================================================================
//  VoxLpcFullBandEnergy
// =========================================================================

double VoxLpcFullBandEnergy(double r0, int N)
{
    if (r0 <= 0.0 || N <= 0)
        return -90.0;           // floor at -90 dB (effectively silence)
    return 10.0 * log10(r0 / (double)N);
}

// =========================================================================
//  VoxLpcLowBandEnergy
// =========================================================================

double VoxLpcLowBandEnergy(const double *r, int order, int N)
{
    double Elow = 0.0;

    // LBF_CORR[k] * r[k]  for k = 0..min(order, LBF_ORDER)
    int kmax = order < LBF_ORDER ? order : LBF_ORDER;

    //
    // Elow = Σ LBF_CORR[k] * r[k]
    //
    // Note: the G.729B formulation uses a symmetric weighting:
    //   r_low[k] = Σ_{j=-12}^{12} LBF_CORR[|j|] * r[|k-j|]
    //
    // For the VAD decision the simplified version (direct weighted sum
    // of the frame autocorrelation) is sufficient because the LBF_CORR
    // filter acts as a low-pass in the autocorrelation domain.
    //
    for (int k = 0; k <= kmax; k++)
        Elow += LBF_CORR[k] * r[k];

    if (Elow <= 0.0)
        return -90.0;
    return 10.0 * log10(Elow / (double)N);
}

// =========================================================================
//  VoxLpcZeroCrossings
// =========================================================================

double VoxLpcZeroCrossings(const short *x, int N)
{
    if (N < 2)
        return 0.0;

    int crossings = 0;

    for (int i = 1; i < N; i++)
    {
        // Sign change detection
        if ((x[i] >= 0 && x[i - 1] < 0) || (x[i] < 0 && x[i - 1] >= 0))
            crossings++;
    }

    return (double)crossings / (double)(N - 1);
}

// =========================================================================
//  VoxLpcRc1
// =========================================================================

double VoxLpcRc1(const double *rc)
{
    return fabs(rc[1]);
}

// =========================================================================
//  VoxLpcPredictionGain
// =========================================================================

double VoxLpcPredictionGain(double r0, double residual)
{
    if (r0 <= 0.0 || residual <= 0.0)
        return 0.0;

    double gain = r0 / residual;
    if (gain <= 1.0)
        return 0.0;

    return 10.0 * log10(gain);
}

// =========================================================================
//  VoxLpcPitchStrength
// =========================================================================

double VoxLpcPitchStrength(const short *x, int N, int min_lag, int max_lag)
{
    if (N <= max_lag || min_lag < 1)
        return 0.0;

    // Energy at lag 0
    double r0 = 0.0;
    for (int n = 0; n < N; n++)
        r0 += (double)x[n] * (double)x[n];

    if (r0 <= 0.0)
        return 0.0;

    // Search for strongest periodicity in the pitch range
    double max_corr = 0.0;
    for (int lag = min_lag; lag <= max_lag; lag++)
    {
        double rk = 0.0;
        for (int n = 0; n < N - lag; n++)
            rk += (double)x[n] * (double)x[n + lag];

        double corr = rk / r0;
        if (corr > max_corr)
            max_corr = corr;
    }

    return max_corr;
}
