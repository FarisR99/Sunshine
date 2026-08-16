# nvidia-surround-toggle

A small Windows CLI that enables/disables an **NVIDIA Surround (Mosaic)** span via NVAPI, so a
Sunshine per-app `prep-cmd` can turn Surround on before a game launches and tear it down when the
game exits. It pairs with Sunshine's per-app **Capture Crop Rect** field to stream only one half
of the span (e.g. a dummy-plug display) while the other half (a TV) stays local.

> ⚠️ **Best-effort / undocumented API.** NVAPI Mosaic is sparsely documented and its exact flag,
> struct-version, and GPU-class (GeForce vs Quadro) requirements vary across driver versions. This
> tool has to be validated on the target machine. It defaults to a **dry run** and refuses to
> change anything unless you pass `--apply`.

## Build (MSYS2 UCRT64)

It reuses the NVAPI headers vendored in the Sunshine repo (`third-party/nvapi`), so no separate
NVAPI download is needed.

```bash
cd contrib/nvidia-surround-toggle
cmake -B build -G Ninja -S .
ninja -C build
# -> build/nvidia-surround-toggle.exe
```

NVAPI is loaded dynamically from `nvapi64.dll` (System32) at runtime — nothing is linked against
`nvapi64.lib`, so this builds cleanly under MinGW/GCC just like Sunshine itself.

## Use

Run everything from an **elevated** shell (changing display topology needs admin rights).

1. **Find your display ids:**
   ```bash
   ./build/nvidia-surround-toggle.exe list
   ```
   Note the `displayId=0x........` for the TV and for the dummy plug.

2. **Dry-run the enable** (validates, changes nothing):
   ```bash
   ./build/nvidia-surround-toggle.exe enable --tv-id 0x<TV> --dummy-id 0x<DUMMY> \
       --width 3840 --height 2160 --refresh 120
   ```
   If it prints `Dry run OK (grid is valid)`, apply it:
   ```bash
   ./build/nvidia-surround-toggle.exe enable --tv-id 0x<TV> --dummy-id 0x<DUMMY> \
       --width 3840 --height 2160 --refresh 120 --apply
   ```
   If validation reports `errorFlags` or `SetDisplayGrids` fails, try adding `--base-mosaic`
   (Quadro/NVS Base Mosaic path) — GeForce vs Quadro differ here.

3. **Disable** (default returns to TV-only, dummy inactive):
   ```bash
   ./build/nvidia-surround-toggle.exe disable --tv-id 0x<TV> --apply
   ```
   Use `--keep-extended --dummy-id 0x<DUMMY>` instead if you want both displays to remain active
   as separate extended monitors after tearing the span down.

## Idempotent / multi-instance wrapper scripts

Two batch wrappers (`surround_enable.bat`, `surround_disable.bat`) live alongside this tool and are
the **recommended** prep-cmds. They add the safety the raw exe lacks:

- **`surround_enable.bat`** — enables Surround **only if it isn't already on**. It runs `list`, and
  if a Mosaic grid with 2+ displays is already active it does nothing and exits `0` (so the app still
  launches). Otherwise it runs `enable … --apply`. All arguments are forwarded verbatim to `enable`.

  ```bat
  surround_enable.bat --tv-id 0x<TV> --dummy-id 0x<DUMMY> --width 3840 --height 2160 --refresh 120
  ```

- **`surround_disable.bat`** — tears the span down **only when nothing is streaming**. It first
  checks whether any Sunshine instance is streaming; if so it leaves Surround up and exits `0`.
  Otherwise it stops the game (`--game <exe>`) and runs `disable … --apply`. Multiple instances are
  supported: pass their base ports via `--ports`. Everything after `--` is forwarded to `disable`.

  ```bat
  surround_disable.bat --game "Stardew Valley.exe" --ports 47989,48989 -- --tv-id 0x<TV>
  ```

  Streaming detection is exact, not a guess: Sunshine binds its per-session video/control/audio UDP
  ports (`base+9` / `base+10` / `base+11`) **only while a stream is active** — the broadcast context
  in `src/stream.cpp` is a ref-counted resource created on the first session and destroyed on the
  last. On Windows, mDNS is handled by the OS (`DnsServiceRegister`), so an idle Sunshine binds none
  of these ports. `surround_disable.bat` therefore treats a bound streaming port as "that instance is
  streaming". For each base port `B` in `--ports` it probes `B+9`, `B+10`, `B+11` via `netstat`.

Both scripts auto-locate `nvidia-surround-toggle.exe` (under `build\` next to the script, or under
`contrib\nvidia-surround-toggle\build\` when run from the repo root); override with the
`SURROUND_TOGGLE_EXE` environment variable.

## Wiring into Sunshine

On the Stardew Valley app entry in the Sunshine web UI (using the wrapper scripts above):

- **Prep command → Do:**
  `C:\dev\Sunshine\surround_enable.bat --tv-id 0x<TV> --dummy-id 0x<DUMMY> --width 3840 --height 2160 --refresh 120`
- **Prep command → Undo:**
  `C:\dev\Sunshine\surround_disable.bat --game "Stardew Valley.exe" --ports 47989 -- --tv-id 0x<TV>`
- **Elevated:** enable it if `enable/disable --apply` fails without admin rights on your driver.
- **Capture Crop Rect** (same app entry): `3840,0,3840,2160` to stream only the right (dummy) half.
- Also set global **`dd_configuration_option` = disabled** so Sunshine's own display prep doesn't
  fight the Mosaic grid.

To wire the raw exe directly instead (no idempotency / no streaming guard), use
`nvidia-surround-toggle.exe enable … --apply` for Do and `… disable … --apply` for Undo.

## Known limitations / things to verify on hardware

- **Struct versions:** the tool sets `NV_MOSAIC_GRID_TOPO_VER`, `NVAPI_MOSAIC_DISPLAY_SETTING_VER1`
  (grid setting is a V1 struct even inside the V2 grid), and a locally-defined
  `NV_MOSAIC_GRID_TOPO_DISPLAY_VER2` for each display entry. If any call returns
  `NVAPI_INCOMPATIBLE_STRUCT_VERSION`, these are the first things to adjust.
- **Single-GPU (non-SLI) systems:** the tool always passes
  `NV_MOSAIC_SETDISPLAYTOPO_FLAG_CURRENT_GPU_TOPOLOGY` so validate/set use the current single GPU
  instead of searching for an SLI configuration. Without it, validation fails with
  `NVAPI_NO_ACTIVE_SLI_TOPOLOGY` (-113) even though the grid geometry is valid.
- **GeForce vs Quadro flags:** by default `enable` builds an *immersive-gaming* Mosaic grid
  (`immersiveGaming=1`, `acceleratePrimaryDisplay=1`, `baseMosaic=0`) — this matches what the
  NVIDIA Control Panel produces for consumer Surround on a GeForce GPU (confirmed by dumping an
  NVCP-enabled grid with `list`). `--base-mosaic` switches to the Base Mosaic (Panoramic) path for
  Quadro/NVS boards instead. `--allow-invalid` adds `ALLOW_INVALID` for debugging.
- **`--skip-validate`:** on some drivers `NvAPI_Mosaic_ValidateDisplayGrids` rejects a config the
  driver's `SetDisplayGrids` accepts (and that NVCP enables fine). If validation fails but `list`
  shows the same grid works manually, pass `--skip-validate` to go straight to the set.
- **Disable semantics:** whether a single TV-only grid fully deactivates the dummy locally (the
  stated goal — dummy unusable when idle) depends on the driver. Verify with `list` after
  disabling; if the dummy stays active, use `--keep-extended` or manage the base topology in the
  NVIDIA Control Panel.
- **Crash safety:** if Sunshine or the game is killed between the `do` and `undo` commands, the
  `undo` never runs and Surround stays enabled. Recover by running `disable --apply` by hand.
- **Driver reload:** `SetDisplayGrids` may reload the driver and briefly blank displays.
