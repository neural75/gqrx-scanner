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
//   pitch_strength > 0.35 on 3+ consecutive frames  → +2
//   ZC > 0.55              → +1 (unvoiced fricatives)
//   ZC < 0.12              → +1 (strongly voiced)
//   El − Ef > 4.0 dB       → +1 (formant concentration)
//
// Pitch sustain requirement: an isolated receiver-hiss frame (0.35-0.43
// pitch strength) is not enough to trigger voice.  The same periodic
// structure must persist for 3+ consecutive frames, which real voice
// does but hiss false positives (spaced > 200 ms apart) do not.
//
// Hangover: 2 frames (32 ms) — enough to bridge brief pitch drop-outs
// without self-sustaining on noise false positives.
//

#include <stdbool.h>

#define VOX_HANGOVER   (2)

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
    int consecutive_pitch_frames;  // count of consecutive frames with pitch>0.35
} VoxVADState;

void VoxVADInit(VoxVADState *s);
bool VoxVADUpdate(VoxVADState *s, const VoxVADFeatures *feat);

#endif
