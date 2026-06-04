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
  - `unit_tests` — 42 unit tests (utility, protocol, filter, file ops) in `tests/unit_tests.c`
  - `sweep_test` — 3 sweep integration tests in `tests/sweep_test.c` (Linux only; uses `--wrap`)
- **TESTING_BUILD macro**: defined for all test binaries; `main()` in `gqrx-scan.c` is wrapped in `#ifndef TESTING_BUILD`; also adds `g_testing_max_full_sweeps` / `g_testing_sweep_full_count` globals for sweep test exit control
- **Protocol tests (16 cases)**: `gqrx-prot.c` functions tested via `-Wl,--wrap=write,--wrap=read` (Linux only); mock in `tests/mocks/mock_socket.c` replaces `write`/`read` with `__wrap_write`/`__wrap_read`
- **Sweep integration tests (Linux only)**: load `tests/profiles/*.txt` profiles (synthetic spectrum), run `ScanFrequenciesInRange()` for 2 full sweeps, verify `SavedFrequencies[]` hits within tolerance and parsed stdout matches
- **Mocks**: `tests/mocks/mock_socket.c` — protocol-aware write (parses `F/l/l SQL/L SQL/f/U RECORD`), profile-aware read with noise jitter; `tests/sweep_mock.c` — wraps `usleep/select/kbhit/fgetc/tcgetattr/tcsetattr`
- **Profiles**: `tests/profiles/*.txt` — self-documented format (NOISE_FLOOR, NOISE_SPAN, SQUELCH, freq/level points, EXPECT lines)
- **Test timeout**: 30 seconds per suite
- **Runtime**: Gqrx must be running with remote control enabled on port 7356 for the real app; tests use mocks

## Architecture
- **2 source files**: `gqrx-scan.c` (~1720 lines — CLI, scan logic, freq management, global state helpers), `gqrx-prot.c` (251 lines — TCP/Gqrx remote protocol)
- **Global state helpers** (in `gqrx-scan.h/c`, available in both production and test builds):
  - `SetOptDefaults()` — zeroes Saved/Banned frequencies, resets all `opt_*` vars to sweep-test defaults
  - `FreeFrequencies()` — frees `Frequencies[i].tags` + the `Frequencies` array, sets pointer to NULL
  - `ResetOptTags()` — frees all `opt_tags[i]` entries, resets `opt_tag_max` to 0
- **Header**: `gqrx-prot.h` — `bool` type requires `<stdbool.h>` included before it; LSP may flag it as unknown if the header is analyzed alone
- **key constants**: `BUFSIZE=1024`, `FREQ_MAX=4096`, `SAVED_FREQ_MAX=1000`, `TAG_MAX=100`, `g_default_scan_bw=10000`, `g_portno=7356`
- **`freq_t`**: `typedef unsigned long long`

## Style
- C99, 4-space indent, no tabs
- snake_case for functions/variables, UPPER_CASE for macros
- `#ifndef OSX` guards for Linux-specific headers (`<arpa/inet.h>`, `<netdb.h>`, `<netinet/in.h>`)
- `error()` wraps `perror()` then `exit()` — use `return false` for recoverable failures
- Prefer `strncpy` over `strcpy`, check BUFSIZE bounds
