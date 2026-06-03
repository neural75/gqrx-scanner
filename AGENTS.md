# Agent Guidelines for gqrx-scanner

## Build
- **Build**: `cmake . && make` (CMake >= 3.5.0)
- **macOS**: `cmake . -DCMAKE_C_FLAGS="-DOSX" && make`
- **Install/Uninstall**: `sudo make install` / `sudo make uninstall`
- **Clean**: `make clean`

## Test
- **All tests**: `cmake . && make && ctest --output-on-failure --verbose`
- **Requires**: `libcmocka-dev` (`sudo apt-get install libcmocka-dev`)
- **cmocka is optional** — build succeeds without it (tests just get disabled)
- **Test binary**: `tests/all_tests.c` (34 cmocka cases, links `gqrx-scan.c` + `gqrx-prot.c` + `tests/mocks/mock_socket.c`)
- **TESTING_BUILD macro**: defined only for the test binary; `main()` in `gqrx-scan.c` is wrapped in `#ifndef TESTING_BUILD`
- **Protocol tests (13 cases)**: `gqrx-prot.c` functions tested via `-Wl,--wrap=write,--wrap=read` (Linux only); mock in `tests/mocks/mock_socket.c` replaces `write`/`read` with `__wrap_write`/`__wrap_read`
- **Legacy files**: `tests/test_utils.c` and `tests/test_file_ops.c` have their own `main()` — not wired into CTest
- **Runtime**: Gqrx must be running with remote control enabled on port 7356 for the real app; tests use mocks

## Architecture
- **2 source files**: `gqrx-scan.c` (1692 lines — CLI, scan logic, freq management), `gqrx-prot.c` (251 lines — TCP/Gqrx remote protocol)
- **Header**: `gqrx-prot.h` — `bool` type requires `<stdbool.h>` included before it; LSP may flag it as unknown if the header is analyzed alone
- **key constants**: `BUFSIZE=1024`, `FREQ_MAX=4096`, `SAVED_FREQ_MAX=1000`, `TAG_MAX=100`, `g_default_scan_bw=10000`, `g_portno=7356`
- **`freq_t`**: `typedef unsigned long long`

## Style
- C99, 4-space indent, no tabs
- snake_case for functions/variables, UPPER_CASE for macros
- `#ifndef OSX` guards for Linux-specific headers (`<arpa/inet.h>`, `<netdb.h>`, `<netinet/in.h>`)
- `error()` wraps `perror()` then `exit()` — use `return false` for recoverable failures
- Prefer `strncpy` over `strcpy`, check BUFSIZE bounds
