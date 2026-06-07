# VAD Test Samples

Raw s16 8 kHz mono PCM files used by `tests/vad_test.c` to validate the
pitch-based VAD pipeline.

## Prerequisites

- PipeWire with `pw-cat` (`pipewire-bin`)
- The intercept virtual sink must be running:
  ```
  ./gqrx-scan-setup-audio.sh
  ```
- `alsa-utils` for playback via `aplay`

## Capturing a Sample

Point your demodulator (DSD, GQRX, etc.) at the desired signal source
(e.g. a busy repeater, a dead frequency for noise), then run:

```bash
timeout 5 pw-cat --record --format=s16 --rate=8000 --channels=1 --raw \
  --properties="stream.capture.sink=true target.object=gqrx-scanner-intercept" \
  tests/vox/voice_with_2s_pause.raw
```

Adjust the `timeout` duration as needed.  The file will be raw s16 little-endian,
mono, 8000 Hz.  No WAV headers.

## Playing Back a Sample

```bash
aplay -f S16_LE -r 8000 -c 1 tests/vox/voice_with_3s_pause.raw
```

## Sample Inventory

| File | Content | Expected VAD behaviour |
|---|---|---|
| `noise_high.raw` | Electromagnetic / atmospheric noise on an unused frequency (high gain) | 0 % voice (< 10 % false positives) |
| `noise_low.raw` | Low-level noise floor on an unused frequency (low gain) | 0 % voice (< 5 % false positives) |
| `hiss_low.raw` | Squelch-tail hiss at moderate level | 0 % voice (exact zero) |
| `hiss_high.raw` | Squelch-tail hiss at high level | 0 % voice (exact zero) |
| `voice_clear_some_hiss.raw` | Clear speech with a mild hiss background | > 90 % voice |
| `voice_low.raw` | Weak / distant speech signal | > 90 % voice |
| `voice_with_3s_pause.raw` | Speech containing a ~3 second pause in the middle | Max consecutive silence ≥ 62 frames (~1 s) |
| `silence.raw` | Digital silence (all zeros) | 0 % voice (exact zero) |

## Generating a Silence File

```bash
dd if=/dev/zero bs=256 count=310 of=tests/vox/silence.raw
```
