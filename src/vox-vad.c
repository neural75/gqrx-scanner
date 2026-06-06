#include "vox-vad.h"
#include <string.h>

// =========================================================================
//  VoxVADInit
// =========================================================================

void VoxVADInit(VoxVADState *s)
{
    s->hangover = 0;
    s->frame_count = 0;
}

// =========================================================================
//  VoxVADUpdate
// =========================================================================

//
// Four criteria (all amplitude-invariant):
//
//  #  Criterion                     Weight
// ——————————————————————————————————————————
//  1  pitch_strength > 0.30         +2
//  2  ZC > 0.50                     +1
//  3  ZC < 0.12                     +1
//  4  El_minus_Ef > 4.0 dB          +1
//
// VOICE if weighted sum ≥ 2.
//
// Hangover: retains VOICE for up to VOX_HANGOVER frames after criteria
// drop below threshold, to avoid clipping syllable endings.
//
bool VoxVADUpdate(VoxVADState *s, const VoxVADFeatures *feat)
{
    s->frame_count++;

    int score = 0;

    // Criterion 1: Pitch periodicity (doubly weighted)
    if (feat->pitch_strength > 0.30)
        score += 2;

    // Criterion 2: High ZC — unvoiced
    if (feat->ZC > 0.50)
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
