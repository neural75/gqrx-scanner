#ifndef VOX_VAD_H
#define VOX_VAD_H

//
// VoxVAD — Pitch-based voice activity detection
//
// Features (all amplitude-invariant):
//   pitch_strength   — max normalized autocorrelation at pitch lags
//   ZC               — zero-crossing rate
//   El_minus_Ef      — low-band minus full-band energy (dB)
//
// Decision: VOICE if weighted criteria ≥ 2.
//   pitch_strength > 0.30  → +2 (strong indicator)
//   ZC > 0.50              → +1 (unvoiced fricatives)
//   ZC < 0.12              → +1 (strongly voiced)
//   El − Ef > 4.0 dB       → +1 (formant concentration)
//
// Hangover: 6 frames (60 ms) to avoid clipping syllable endings.
//

#include <stdbool.h>

#define VOX_HANGOVER   (6)

//
// Per-frame features.
//
typedef struct {
    double pitch_strength;      // max norm autocorr at pitch lags [0..1]
    double ZC;                  // zero-crossing rate [0..1]
    double El_minus_Ef;         // El - Ef (dB), gain-invariant difference
} VoxVADFeatures;

//
// VoxVAD state.
//
typedef struct {
    int hangover;               // remaining hangover frames
    int frame_count;            // total frames processed
} VoxVADState;

void VoxVADInit(VoxVADState *s);
bool VoxVADUpdate(VoxVADState *s, const VoxVADFeatures *feat);

#endif
