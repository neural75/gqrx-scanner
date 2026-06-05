#ifndef VOX_AUDIO_H
#define VOX_AUDIO_H

//
// VOX / Audio Activity Detection
//
// This module captures raw u8 mono 8 kHz audio from the PulseAudio/PipeWire
// monitor source created by gqrx-scan-setup-audio.sh.  It is used to detect
// whether a frequency is carrying actual audio (speech, tones, etc.) rather
// than just an open carrier (squelch open but silence).
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
// TODO: VoxAudioHasSignal() will be added later to poll for audio energy.
//

#include <stdbool.h>

//
// VoxAudioInit
//   Open the pacat capture pipe from the Gqrx null-sink monitor.
//   Ignores SIGPIPE so that a crashed/disconnected pacat does not kill us.
//   Returns true on success, false on error (pacat not found, Gqrx not
//   streaming, etc.).
//
bool VoxAudioInit(void);

//
// VoxAudioShutdown
//   Close the capture pipe, reap pacat, and restore the original SIGPIPE
//   disposition.  Safe to call even if VoxAudioInit was never called or
//   failed (no-op when already shut down).
//
void VoxAudioShutdown(void);

#endif
