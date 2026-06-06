# Agent Guidelines for gqrx-scanner

## Build
- **Build**: `cmake . && make` (CMake >= 3.5.0)
- **macOS**: `cmake . -DCMAKE_C_FLAGS="-DOSX" && make`
- **check (build + test)**: `make check`
- **Install/Uninstall**: `sudo make install` / `sudo make uninstall`
- **Clean**: `make clean`

## Test
- **All tests**: `make check`
- **Requires**: `libcmocka-dev` (`sudo apt-get install libcmocka-dev`)
- **cmocka is optional** — build succeeds without it (tests just get disabled)
- **Test suites**:
  - `unit_tests` — 46 unit tests (utility, protocol, filter, file ops) in `tests/unit_tests.c`
  - `sweep_test` — 7 sweep integration tests in `tests/sweep_test.c` (Linux only; uses `--wrap`)
  - `vad_test` — VAD tests in `tests/vad_test.c` (plays raw s16 files through the VAD pipeline)

- **TESTING_BUILD macro**: defined for all test binaries; `main()` in `gqrx-scan.c` is wrapped in `#ifndef TESTING_BUILD`; also adds `g_testing_max_full_sweeps` / `g_testing_sweep_full_count` globals for sweep test exit control
- **Protocol tests (16 cases)**: `gqrx-prot.c` functions tested via `-Wl,--wrap=write,--wrap=read` (Linux only); mock in `tests/mocks/mock_socket.c` replaces `write`/`read` with `__wrap_write`/`__wrap_read`
- **Sweep integration tests (Linux only)**: load `tests/profiles/*.txt` profiles (synthetic spectrum), run `ScanFrequenciesInRange()` for 2 full sweeps, verify `SavedFrequencies[]` hits within tolerance and parsed stdout matches
- **Mocks**: `tests/mocks/mock_socket.c` — protocol-aware write (parses `F/l/l SQL/L SQL/f/U RECORD`), profile-aware read with noise jitter; `tests/sweep_mock.c` — wraps `usleep/select/kbhit/fgetc/tcgetattr/tcsetattr`
- **Profiles**: `tests/profiles/*.txt` — self-documented format (NOISE_FLOOR, NOISE_SPAN, SQUELCH, freq/level points, EXPECT lines)
- **Test timeout**: 30 seconds per suite
- **Runtime**: Gqrx must be running with remote control enabled on port 7356 for the real app; tests use mocks

## Architecture
- **Source files** in `src/`: `src/gqrx-scan.c` (~1720 lines — CLI, scan logic, freq management, global state helpers), `src/gqrx-prot.c` (251 lines — TCP/Gqrx remote protocol), `src/vox-audio.c` (PipeWire audio capture for VOX detection), `src/vox-lpc.c` (LPC analysis — autocorrelation, pitch strength, zero-crossings, band energies), `src/vox-vad.c` (pitch-based VAD — 4 criteria, hangover 2 frames)
- **Headers** in `src/`: `src/gqrx-scan.h`, `src/gqrx-prot.h`, `src/vox-audio.h`, `src/vox-lpc.h`, `src/vox-vad.h`
- **Global state helpers** (in `src/gqrx-scan.h/c`, available in both production and test builds):
  - `SetOptDefaults()` — zeroes Saved/Banned frequencies, resets all `opt_*` vars to sweep-test defaults
  - `FreeFrequencies()` — frees `Frequencies[i].tags` + the `Frequencies` array, sets pointer to NULL
  - `ResetOptTags()` — frees all `opt_tags[i]` entries, resets `opt_tag_max` to 0
- **VAD pitch sustain**: pitch_strength > 0.35 requires 2+ consecutive frames to score +2. This suppresses receiver-hiss false positives (isolated periodic frames spaced >200ms apart) while real voice passes through (sustained pitch runs of many frames). Measured on `voice_with_3s_pause.raw`: max consecutive silence improved from 32ms to 1056ms (above the 992ms/62-frame test floor for `-l 1000`).
- **Header**: `src/gqrx-prot.h` — `bool` type requires `<stdbool.h>` included before it; LSP may flag it as unknown if the header is analyzed alone
- **key constants**: `BUFSIZE=1024`, `FREQ_MAX=4096`, `SAVED_FREQ_MAX=1000`, `TAG_MAX=100`, `g_default_scan_bw=10000`, `g_portno=7356`
- **`freq_t`**: `typedef unsigned long long`

## Code Comments
All source files must be thoroughly commented.  Every function, non-trivial block, and error-handling path must explain:
- *What* it does
- *Why* it is done this way (especially if the approach is non-obvious)
- *Error conditions* that are handled and how

This is essential because the codebase is maintained across multiple sessions and contributors.

## Commit
- **NEVER commit or push without explicit instruction** — the user says "commit" when ready, no need to ask after every change.

## Style
- C99, 4-space indent, no tabs
- snake_case for functions/variables, UPPER_CASE for macros
- `#ifndef OSX` guards for Linux-specific headers (`<arpa/inet.h>`, `<netdb.h>`, `<netinet/in.h>`)
- `error()` wraps `perror()` then `exit()` — use `return false` for recoverable failures
- Prefer `strncpy` over `strcpy`, check BUFSIZE bounds
- **Maintain encapsulation and proper abstractions** — do not inline complex computations into loops or caller functions. Each non-trivial block of work belongs in its own well-named function, even if it is only called once. The code must remain readable and modular.
