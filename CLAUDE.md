# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

Sunshine is a self-hosted, cross-platform (Windows, Linux, macOS, FreeBSD) game stream host for Moonlight clients.
It's a C++23 application (CMake/Ninja build) with a Vue 3 + Vite web UI for configuration and pairing, plus Python
tooling (via `uv`) for localization and packaging helpers.

## Build

Requires CMake > 3.25 and a submodule checkout (`git clone --recurse-submodules`, or
`git submodule update --init --recursive` after the fact).

```bash
cmake -B build -G Ninja -S .
ninja -C build
```

Windows builds must happen on-target (no cross-compilation) inside MSYS2. Prefix every command with:
```
C:\msys64\msys2_shell.cmd -defterm -here -no-start -ucrt64 -c "<command>"
```
Prefix build directories with `cmake-build-` on Windows (e.g. `cmake-build-debug`). Windows static-Qt builds need
`-DCMAKE_PREFIX_PATH="${MINGW_PREFIX}/qt6-static" -DSUNSHINE_USE_STATIC_QT=ON`.

Useful CMake options (full list in `cmake/prep/options.cmake`): `BUILD_TESTS`, `BUILD_DOCS`, `BUILD_WERROR`,
`SUNSHINE_ENABLE_TRAY`, `SUNSHINE_CONFIGURE_ONLY`.

### Web UI only

```bash
cmake -B build -G Ninja -S . --target web-ui
ninja -C build web-ui
```
or, without CMake: `npm run dev` (watch mode) / `npm run build`. Web UI source lives in
`src_assets/common/assets/web`; Vite entry points and output paths are defined in `vite.config.js`.

## Tests

C++ unit/integration tests use GoogleTest (submodule, no separate install needed) and live under `tests/`
(`tests/unit`, `tests/integration`). They compile into the main build (disable via `-DBUILD_TESTS=OFF`).

```bash
./build/tests/test_sunshine                 # run all tests
./build/tests/test_sunshine --gtest_filter=TestSuite.TestName   # run a single test
./build/tests/test_sunshine --help           # list all gtest options
```

On Windows the binary is `build/tests/test_sunshine.exe` inside the MSYS2 shell.

## Linting / formatting

C/C++ style is enforced by `.clang-format` (do not hand-edit — it's centrally managed upstream, generated from
CLion's code style settings). Apply it via the LizardByte tooling (needs `uv sync --locked` first):

```bash
uv sync --locked
uv run --locked --no-sync lb-update-clang-format
```

## Architecture

### Core C++ application (`src/`)

- `main.cpp` — entry point, orchestrates startup.
- `stream.cpp/h`, `rtsp.cpp/h`, `video.cpp/h`, `audio.cpp/h`, `input.cpp/h` — the streaming pipeline: RTSP session
  setup, video/audio capture+encode, and virtual input injection back to the OS.
- `nvhttp.cpp/h`, `httpcommon.cpp/h`, `confighttp.cpp/h` — HTTP servers: `nvhttp` implements the GameStream/NVIDIA
  Shield-compatible pairing & control protocol Moonlight clients speak; `confighttp` serves the Web UI/config API.
- `network.cpp/h`, `upnp.cpp/h`, `crypto.cpp/h` — networking, UPnP port mapping, and TLS/certs for pairing.
- `process.cpp/h` — launching/managing configured applications and games.
- `display_device.cpp/h` — display mode switching, delegates to the `libdisplaydevice` submodule.
- `config.cpp/h` — config file schema, parsing, and defaults; kept in lockstep with `docs/configuration.md` and the
  Web UI's `src_assets/common/assets/web/config.html` / `configs/tabs/General.vue` (see integration tests below).
- `system_tray.cpp/h` — Qt6-based tray icon, shared across all desktop platforms.
- `entry_handler.cpp/h` — CLI argument/subcommand handling.
- `globals.h` — process-wide globals, including the `mail::` namespace, a pub/sub-style mailbox system
  (`safe::mail_t`) used to pass messages (shutdown, video/audio packets, IDR requests, etc.) between subsystems
  instead of direct coupling.
- `thread_pool.h`, `task_pool.h`, `sync.h`, `thread_safe.h`, `round_robin.h` — concurrency primitives used
  throughout the streaming pipeline.
- `src/nvenc/` — NVENC (NVIDIA hardware encoder) integration, with separate D3D11/CUDA interop backends.
- `src/platform/` — platform abstraction layer. `common.h` declares the interface; `windows/`, `linux/`, `macos/`
  hold OS-specific implementations for capture (KMS/X11/Wayland/portal/VAAPI on Linux, WGC/DXGI on Windows,
  AVFoundation on macOS), audio, input injection, and publish (mDNS/service advertisement). New platform code goes
  behind this abstraction, not behind `#ifdef` scattered through shared code where avoidable.

### CMake structure (`cmake/`)

`CMakeLists.txt` includes modules in a fixed order: `prep/` (version, options, init, special package config) →
`prep/constants.cmake` → `macros/` → `dependencies/` → `compile_definitions/` → `targets/` → `packaging/`. Each of
those directories has a `common.cmake` plus per-platform files (`linux.cmake`, `windows.cmake`, `macos.cmake`,
`unix.cmake`) that are conditionally included. Third-party libraries are fetched via CPM
(`cmake/cpm/CPM.cmake`, locked with `package-lock.cmake`).

### Web UI (`src_assets/common/assets/web/`)

Vue 3 + Vite + Bootstrap 5. Icons from Lucide and Simple Icons. Multi-page app — each top-level page
(`index.html`, `apps.html`, `config.html`, `featured.html`, `password.html`, `pin.html`, `troubleshooting.html`,
`welcome.html`, `logout.html`) is its own Vite entry point (see `vite.config.js`). `template_header.html` /
`template_header_main.html` are shared EJS-templated fragments injected via `vite-plugin-ejs`. Per-tab config UI
lives in `configs/tabs/`.

### Third-party submodules (`third-party/`)

Notable ones: `libdisplaydevice` (display mode switching), `moonlight-common-c` (wire protocol), `inputtino`
(virtual gamepad/input emulation on Linux), `ViGEmClient` (virtual gamepad on Windows), `tray` (cross-platform
system tray), `nvapi` (NVIDIA control panel), `Simple-Web-Server`, `lizardbyte-common` (shared LizardByte tooling,
including GoogleTest and the clang-format/localization scripts used above).

## Conventions (from AGENTS.md — apply repo-wide)

- **Doxygen is mandatory.** The build fails without it. Use `/** @brief ... @param ... @return ... */` block
  comments for functions/classes/structs, and `///< ...` (never `/**< ... */`) for inline trailing comments.
- **Follow `.clang-format`** for all C/C++ code; don't hand-format.
- **Localization**: only ever add/update strings in the `en` locale (`en.json` for web UI, extracted `en` strings
  for C++ via `boost::locale::translate`). Never touch other language files or variants like `en-US` — those are
  managed via CrowdIn and pushed by automation.
- **Tests**: add or update tests for new/modified code; target 100% coverage on changed lines. Tests live in
  `tests/unit` and `tests/integration`, built into the `test_sunshine` executable.
- **Never create GitHub issues or PRs** in the LizardByte org from an agent. If asked to open one, do so in the
  user's own fork instead.
