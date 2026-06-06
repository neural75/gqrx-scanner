#include "vox-vad.h"
#include <string.h>

// =========================================================================
//  VoxVADInit
// =========================================================================

void VoxVADInit(VoxVADState *s)
{
    s->hangover = 0;
    s->frame_count = 0;
    s->consecutive_pitch_frames = 0;
}

// =========================================================================
//  VoxVADUpdate
// =========================================================================

//
// Four criteria (all amplitude-invariant):
//
//  #  Criterion                     Weight
// ——————————————————————————————————————————
//  1  pitch_strength > 0.35         +2 (requires 3+ consecutive frames)
//  2  ZC > 0.55                     +1
//  3  ZC < 0.12                     +1
//  4  El_minus_Ef > 4.0 dB          +1
//
// VOICE if weighted sum ≥ 2.
//
// Pitch-sustain: an isolated receiver-hiss frame with pitch 0.35–0.43
// does NOT count toward the score.  The +2 pitch weight only applies
// when pitch_strength exceeds threshold on 3+ consecutive frames.
// Receiver hiss usually produces isolated false positives spaced
// > 200 ms apart, so the sustain requirement eliminates them without
// affecting real speech (which fires pitch on nearly every voiced
// frame).
//
// Hangover: retains VOICE for up to VOX_HANGOVER frames after criteria
// drop below threshold, to avoid clipping syllable endings.  With 16 ms
// frames, 2 frames gives 32 ms of hangover — enough to bridge brief
// drop-outs in pitch detection without self-sustaining on noise.
//
bool VoxVADUpdate(VoxVADState *s, const VoxVADFeatures *feat)
{
    s->frame_count++;

    // Track consecutive frames with pitch > threshold.
    // Reset on any frame below threshold — isolated hiss false
    // positives are suppressed; sustained voice passes through.
    if (feat->pitch_strength > 0.35)
        s->consecutive_pitch_frames++;
    else
        s->consecutive_pitch_frames = 0;

    int score = 0;

    // Criterion 1: Pitch periodicity — requires 2+ consecutive frames
    // to avoid false positives from receiver-hiss periodic structure.
    if (s->consecutive_pitch_frames >= 3)
        score += 2;

    // Criterion 2: High ZC — unvoiced (e.g. fricatives)
    if (feat->ZC > 0.55)
        score++;

    // Criterion 3: Very low ZC — strongly voiced
    if (feat->ZC < 0.12)
        score++;

    // Criterion 4: Low-band energy concentration
    if (feat->El_minus_Ef > 4.0)
        score++;

    bool voice = (score >= 2);

    // Hangover
    if (voice)
        s->hangover = VOX_HANGOVER;
    else if (s->hangover > 0)
    {
        s->hangover--;
        voice = true;
    }

    return voice;
}
