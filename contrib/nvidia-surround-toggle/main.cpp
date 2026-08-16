/**
 * @file main.cpp
 * @brief NVIDIA Surround (Mosaic) enable/disable/list CLI for use from Sunshine prep-cmd hooks.
 *
 * This standalone tool toggles an NVIDIA Surround span on and off via the NVAPI Mosaic
 * "display grid" API, so a per-app Sunshine prep-cmd can enable Surround before a game launches
 * and tear it down when the game exits. It is intentionally NOT part of the Sunshine build.
 *
 * Usage:
 *   nvidia-surround-toggle list
 *   nvidia-surround-toggle enable  --tv-id 0x.. --dummy-id 0x.. [--width 3840] [--height 2160]
 *                                  [--refresh 120] [--bpp 32] [--base-mosaic] [--apply]
 *   nvidia-surround-toggle disable --tv-id 0x.. [--dummy-id 0x..] [--keep-extended]
 *                                  [--width ..] [--height ..] [--refresh ..] [--bpp 32] [--apply]
 *
 * WITHOUT --apply the tool performs a *dry run*: it builds the requested grid, validates it via
 * NvAPI_Mosaic_ValidateDisplayGrids, and prints the result WITHOUT changing anything. Pass
 * --apply only once a dry run reports the grid as valid.
 *
 * WARNING: NVAPI Mosaic is sparsely documented and its exact flag/version requirements vary by
 * GPU class (GeForce vs Quadro) and driver version. Treat this as a best-effort helper that must
 * be validated on the target hardware. See README.md.
 */
// standard includes (before nvapi so the stdlib isn't parsed under nvapi's SAL macros)
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

// nvapi includes.
// Under GCC/MinGW, nvapi.h transitively includes nvapi_lite_salend.h partway through, which
// #undefs the SAL macros (e.g. __success) that the rest of nvapi.h still needs -- breaking
// NVAPI_INTERFACE. Defining __NVAPI_EMPTY_SAL makes that internal salend a no-op so the SAL
// macros stay defined throughout nvapi.h; we then include salend.h explicitly afterward to clean
// them up. Mirrors src/platform/windows/nvprefs/driver_settings.h.
#if defined(__GNUC__)
  #define __NVAPI_EMPTY_SAL
#endif
#include <nvapi.h>
#if defined(__GNUC__)
  #undef __NVAPI_EMPTY_SAL
  #include <nvapi_lite_salend.h>
#endif

// The V2 grid struct embeds V2 display entries, but NVIDIA does not ship a version macro for the
// display sub-struct. Define it from the struct's V2 tag so each entry's `version` can be set.
#ifndef NV_MOSAIC_GRID_TOPO_DISPLAY_VER2
  #define NV_MOSAIC_GRID_TOPO_DISPLAY_VER2 MAKE_NVAPI_VERSION(NV_MOSAIC_GRID_TOPO_DISPLAY_V2, 2)
#endif

namespace {
  /**
   * @brief Print an NVAPI status code with its human-readable message.
   *
   * @param status The NVAPI status to describe.
   * @param context Short label describing which call produced the status.
   */
  void log_status(NvAPI_Status status, const char *context) {
    NvAPI_ShortString message = {};
    NvAPI_GetErrorMessage(status, message);
    std::fprintf(stderr, "%s: NVAPI status %d (%s)\n", context, static_cast<int>(status), message);
  }

  /**
   * @brief Find the value following a `--flag` argument.
   *
   * @param argc Argument count.
   * @param argv Argument vector.
   * @param flag The flag to search for (e.g. "--tv-id").
   * @return The value string if present, otherwise std::nullopt.
   */
  std::optional<std::string> arg_value(int argc, char **argv, const char *flag) {
    for (int i = 1; i < argc - 1; ++i) {
      if (std::strcmp(argv[i], flag) == 0) {
        return std::string {argv[i + 1]};
      }
    }
    return std::nullopt;
  }

  /**
   * @brief Return true if a bare `--flag` (no value) is present.
   */
  bool has_flag(int argc, char **argv, const char *flag) {
    for (int i = 1; i < argc; ++i) {
      if (std::strcmp(argv[i], flag) == 0) {
        return true;
      }
    }
    return false;
  }

  /**
   * @brief Parse a display id given as decimal or 0x-prefixed hex.
   *
   * @param value The string to parse.
   * @return The parsed 32-bit id, or std::nullopt on error.
   */
  std::optional<NvU32> parse_id(const std::string &value) {
    try {
      size_t consumed = 0;
      unsigned long parsed = std::stoul(value, &consumed, 0);  // base 0 -> auto-detect 0x / decimal
      if (consumed != value.size()) {
        return std::nullopt;
      }
      return static_cast<NvU32>(parsed);
    } catch (...) {
      return std::nullopt;
    }
  }

  /**
   * @brief Read a `--flag` as an unsigned integer, falling back to a default when absent.
   */
  NvU32 arg_uint(int argc, char **argv, const char *flag, NvU32 fallback) {
    if (auto value = arg_value(argc, argv, flag)) {
      try {
        return static_cast<NvU32>(std::stoul(*value, nullptr, 0));
      } catch (...) {
        std::fprintf(stderr, "Invalid value for %s: '%s' (using default %u)\n", flag, value->c_str(), fallback);
      }
    }
    return fallback;
  }

  /**
   * @brief Fill a single grid-topology display entry.
   */
  void set_display(NV_MOSAIC_GRID_TOPO &grid, NvU32 index, NvU32 display_id) {
    grid.displays[index].version = NV_MOSAIC_GRID_TOPO_DISPLAY_VER2;
    grid.displays[index].displayId = display_id;
    grid.displays[index].overlapX = 0;
    grid.displays[index].overlapY = 0;
    grid.displays[index].rotation = NV_ROTATE_0;
    grid.displays[index].cloneGroup = 0;
  }

  /**
   * @brief Fill a grid's shared display settings (per-display resolution/refresh/bpp).
   */
  void set_display_settings(NV_MOSAIC_GRID_TOPO &grid, NvU32 width, NvU32 height, NvU32 bpp, NvU32 freq) {
    grid.displaySettings.version = NVAPI_MOSAIC_DISPLAY_SETTING_VER1;
    grid.displaySettings.width = width;
    grid.displaySettings.height = height;
    grid.displaySettings.bpp = bpp;
    grid.displaySettings.freq = freq;
  }

  /**
   * @brief Validate a set of grids and, if requested, apply them.
   *
   * @param grids The grid topologies to validate/apply.
   * @param topo_flags NV_MOSAIC_SETDISPLAYTOPO_FLAG_* flags forwarded to validate/set.
   * @param apply When true, call NvAPI_Mosaic_SetDisplayGrids after a successful validation.
   * @param skip_validate When true, skip NvAPI_Mosaic_ValidateDisplayGrids entirely. Useful when
   * validation is stricter than the driver's actual SetDisplayGrids (e.g. single-GPU Base Mosaic
   * that the NVIDIA Control Panel enables fine but ValidateDisplayGrids rejects).
   * @return 0 on success (validated, and applied if requested), non-zero otherwise.
   */
  int validate_and_apply(std::vector<NV_MOSAIC_GRID_TOPO> &grids, NvU32 topo_flags, bool apply, bool skip_validate) {
    if (!skip_validate) {
      std::vector<NV_MOSAIC_DISPLAY_TOPO_STATUS> statuses(grids.size());
      for (auto &status : statuses) {
        status = {};
        status.version = NV_MOSAIC_DISPLAY_TOPO_STATUS_VER;
      }

      NvAPI_Status vresult = NvAPI_Mosaic_ValidateDisplayGrids(topo_flags, grids.data(), statuses.data(), static_cast<NvU32>(grids.size()));
      if (vresult != NVAPI_OK) {
        log_status(vresult, "NvAPI_Mosaic_ValidateDisplayGrids");
      }

      bool valid = (vresult == NVAPI_OK);
      for (size_t i = 0; i < statuses.size(); ++i) {
        std::printf("  grid[%zu]: errorFlags=0x%08X warningFlags=0x%08X\n",
                    i, statuses[i].errorFlags, statuses[i].warningFlags);
        if (statuses[i].errorFlags != 0) {
          valid = false;
        }
      }

      if (!valid) {
        std::fprintf(stderr, "Grid validation failed; not applying. (Try --skip-validate if the "
                             "NVIDIA Control Panel can enable this Surround config manually.)\n");
        return 1;
      }
    } else {
      std::printf("Skipping validation (--skip-validate).\n");
    }

    if (!apply) {
      std::printf("Dry run OK%s. Re-run with --apply to change the display topology.\n",
                  skip_validate ? " (validation skipped)" : " (grid is valid)");
      return 0;
    }

    NvAPI_Status result = NvAPI_Mosaic_SetDisplayGrids(grids.data(), static_cast<NvU32>(grids.size()), topo_flags);
    if (result != NVAPI_OK) {
      log_status(result, "NvAPI_Mosaic_SetDisplayGrids");
      return 1;
    }

    std::printf("Applied %zu grid topolog%s.\n", grids.size(), grids.size() == 1 ? "y" : "ies");
    return 0;
  }

  /**
   * @brief Build the NV_MOSAIC_SETDISPLAYTOPO_FLAG_* set from CLI options.
   *
   * Single-GPU (non-SLI) systems must pass CURRENT_GPU_TOPOLOGY, otherwise validate/set search
   * for an SLI configuration and fail with NVAPI_NO_ACTIVE_SLI_TOPOLOGY. --allow-invalid adds the
   * ALLOW_INVALID flag for debugging (accept a config no GPU topology fully supports).
   */
  NvU32 topo_flags_from_args(int argc, char **argv) {
    NvU32 flags = NV_MOSAIC_SETDISPLAYTOPO_FLAG_CURRENT_GPU_TOPOLOGY;
    if (has_flag(argc, argv, "--allow-invalid")) {
      flags |= NV_MOSAIC_SETDISPLAYTOPO_FLAG_ALLOW_INVALID;
    }
    return flags;
  }

  /**
   * @brief List connected displays (with their display ids) and any current Mosaic grids.
   */
  int cmd_list() {
    NvPhysicalGpuHandle gpus[NVAPI_MAX_PHYSICAL_GPUS] = {};
    NvU32 gpu_count = 0;
    NvAPI_Status result = NvAPI_EnumPhysicalGPUs(gpus, &gpu_count);
    if (result != NVAPI_OK) {
      log_status(result, "NvAPI_EnumPhysicalGPUs");
      return 1;
    }

    std::printf("Connected displays (displayId is what enable/disable expect):\n");
    for (NvU32 g = 0; g < gpu_count; ++g) {
      NvU32 id_count = 0;
      result = NvAPI_GPU_GetConnectedDisplayIds(gpus[g], nullptr, &id_count, 0);
      if (result != NVAPI_OK || id_count == 0) {
        continue;
      }

      std::vector<NV_GPU_DISPLAYIDS> ids(id_count);
      for (auto &id : ids) {
        id = {};
        id.version = NV_GPU_DISPLAYIDS_VER;
      }
      result = NvAPI_GPU_GetConnectedDisplayIds(gpus[g], ids.data(), &id_count, 0);
      if (result != NVAPI_OK) {
        log_status(result, "NvAPI_GPU_GetConnectedDisplayIds");
        continue;
      }

      for (NvU32 i = 0; i < id_count; ++i) {
        std::printf("  GPU %u  displayId=0x%08X  connector=%d  active=%u  connected=%u\n",
                    g, ids[i].displayId, static_cast<int>(ids[i].connectorType),
                    ids[i].isActive, ids[i].isConnected);
      }
    }

    NvU32 grid_count = 0;
    result = NvAPI_Mosaic_EnumDisplayGrids(nullptr, &grid_count);
    if (result == NVAPI_OK && grid_count > 0) {
      std::vector<NV_MOSAIC_GRID_TOPO> grids(grid_count);
      for (auto &grid : grids) {
        grid = {};
        grid.version = NV_MOSAIC_GRID_TOPO_VER;
      }
      result = NvAPI_Mosaic_EnumDisplayGrids(grids.data(), &grid_count);
      if (result == NVAPI_OK) {
        std::printf("Current Mosaic grids: %u\n", grid_count);
        for (NvU32 i = 0; i < grid_count; ++i) {
          std::printf("  grid[%u]: %ux%u, %u display(s), per-display %ux%u@%uHz bpp=%u\n",
                      i, grids[i].rows, grids[i].columns, grids[i].displayCount,
                      grids[i].displaySettings.width, grids[i].displaySettings.height,
                      grids[i].displaySettings.freq, grids[i].displaySettings.bpp);
          std::printf("      flags: baseMosaic=%u immersiveGaming=%u applyWithBezelCorrect=%u acceleratePrimaryDisplay=%u\n",
                      grids[i].baseMosaic, grids[i].immersiveGaming, grids[i].applyWithBezelCorrect, grids[i].acceleratePrimaryDisplay);
          for (NvU32 d = 0; d < grids[i].displayCount; ++d) {
            std::printf("      display[%u]: displayId=0x%08X overlapX=%d overlapY=%d rotation=%d\n",
                        d, grids[i].displays[d].displayId, grids[i].displays[d].overlapX,
                        grids[i].displays[d].overlapY, static_cast<int>(grids[i].displays[d].rotation));
          }
        }
      }
    } else {
      std::printf("Current Mosaic grids: 0 (no Surround span active)\n");
    }

    return 0;
  }

  /**
   * @brief Enable a 1x2 Surround span across the TV and dummy display.
   */
  int cmd_enable(int argc, char **argv) {
    auto tv = arg_value(argc, argv, "--tv-id");
    auto dummy = arg_value(argc, argv, "--dummy-id");
    if (!tv || !dummy) {
      std::fprintf(stderr, "enable requires --tv-id and --dummy-id (see `list`).\n");
      return 2;
    }
    auto tv_id = parse_id(*tv);
    auto dummy_id = parse_id(*dummy);
    if (!tv_id || !dummy_id) {
      std::fprintf(stderr, "Invalid --tv-id/--dummy-id; expected decimal or 0x-hex.\n");
      return 2;
    }

    NvU32 width = arg_uint(argc, argv, "--width", 3840);
    NvU32 height = arg_uint(argc, argv, "--height", 2160);
    NvU32 freq = arg_uint(argc, argv, "--refresh", 120);
    NvU32 bpp = arg_uint(argc, argv, "--bpp", 32);

    NV_MOSAIC_GRID_TOPO grid = {};
    grid.version = NV_MOSAIC_GRID_TOPO_VER;
    grid.rows = 1;
    grid.columns = 2;
    grid.displayCount = 2;
    if (has_flag(argc, argv, "--base-mosaic")) {
      // Base Mosaic (Panoramic) -- the Quadro/NVS path.
      grid.baseMosaic = 1;
    } else {
      // Consumer NVIDIA Surround is the immersive-gaming Mosaic mode: this matches the flags of a
      // grid enabled via the NVIDIA Control Panel on a GeForce GPU (baseMosaic=0, immersiveGaming=1,
      // acceleratePrimaryDisplay=1). Without one of these set, validate/set fall back to plain
      // Mosaic-SLI and fail on single-GPU systems with NVAPI_NO_ACTIVE_SLI_TOPOLOGY.
      grid.immersiveGaming = 1;
      grid.acceleratePrimaryDisplay = 1;
    }
    // displays are laid out [(row * columns) + column]; index 0 = left (TV), index 1 = right (dummy).
    set_display(grid, 0, *tv_id);
    set_display(grid, 1, *dummy_id);
    set_display_settings(grid, width, height, bpp, freq);

    std::printf("Enable Surround: 1x2 grid, per-display %ux%u@%uHz (span %ux%u)\n",
                width, height, freq, width * 2, height);
    std::printf("  left  displayId=0x%08X (TV)\n", *tv_id);
    std::printf("  right displayId=0x%08X (dummy)\n", *dummy_id);

    std::vector<NV_MOSAIC_GRID_TOPO> grids {grid};
    return validate_and_apply(grids, topo_flags_from_args(argc, argv), has_flag(argc, argv, "--apply"), has_flag(argc, argv, "--skip-validate"));
  }

  /**
   * @brief Tear the Surround span down. By default drops back to the TV as the sole display
   * (dummy becomes inactive); with --keep-extended, emits TV and dummy as two standalone 1x1
   * grids (both remain active as extended displays).
   */
  int cmd_disable(int argc, char **argv) {
    auto tv = arg_value(argc, argv, "--tv-id");
    if (!tv) {
      std::fprintf(stderr, "disable requires --tv-id (see `list`).\n");
      return 2;
    }
    auto tv_id = parse_id(*tv);
    if (!tv_id) {
      std::fprintf(stderr, "Invalid --tv-id; expected decimal or 0x-hex.\n");
      return 2;
    }

    NvU32 width = arg_uint(argc, argv, "--width", 3840);
    NvU32 height = arg_uint(argc, argv, "--height", 2160);
    NvU32 freq = arg_uint(argc, argv, "--refresh", 120);
    NvU32 bpp = arg_uint(argc, argv, "--bpp", 32);

    bool keep_extended = has_flag(argc, argv, "--keep-extended");

    std::vector<NV_MOSAIC_GRID_TOPO> grids;

    NV_MOSAIC_GRID_TOPO tv_grid = {};
    tv_grid.version = NV_MOSAIC_GRID_TOPO_VER;
    tv_grid.rows = 1;
    tv_grid.columns = 1;
    tv_grid.displayCount = 1;
    set_display(tv_grid, 0, *tv_id);
    set_display_settings(tv_grid, width, height, bpp, freq);
    grids.push_back(tv_grid);

    if (keep_extended) {
      auto dummy = arg_value(argc, argv, "--dummy-id");
      auto dummy_id = dummy ? parse_id(*dummy) : std::nullopt;
      if (!dummy_id) {
        std::fprintf(stderr, "--keep-extended requires a valid --dummy-id.\n");
        return 2;
      }
      NV_MOSAIC_GRID_TOPO dummy_grid = {};
      dummy_grid.version = NV_MOSAIC_GRID_TOPO_VER;
      dummy_grid.rows = 1;
      dummy_grid.columns = 1;
      dummy_grid.displayCount = 1;
      set_display(dummy_grid, 0, *dummy_id);
      set_display_settings(dummy_grid, width, height, bpp, freq);
      grids.push_back(dummy_grid);
    }

    std::printf("Disable Surround: %s\n", keep_extended ? "TV + dummy as separate extended displays" : "TV only (dummy inactive)");
    return validate_and_apply(grids, topo_flags_from_args(argc, argv), has_flag(argc, argv, "--apply"), has_flag(argc, argv, "--skip-validate"));
  }

  void print_usage() {
    std::printf(
      "nvidia-surround-toggle - enable/disable an NVIDIA Surround (Mosaic) span\n\n"
      "  list                              List display ids and current Mosaic grids\n"
      "  enable  --tv-id ID --dummy-id ID  Span TV+dummy into one Surround display\n"
      "  disable --tv-id ID [--dummy-id ID] Tear the span down\n\n"
      "Options: --width --height --refresh --bpp --base-mosaic --keep-extended --allow-invalid --skip-validate --apply\n"
      "Without --apply the tool validates and prints only (dry run). See README.md.\n");
  }
}  // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    print_usage();
    return 2;
  }

  const std::string mode = argv[1];
  if (mode == "-h" || mode == "--help") {
    print_usage();
    return 0;
  }

  NvAPI_Status init = NvAPI_Initialize();
  if (init != NVAPI_OK) {
    log_status(init, "NvAPI_Initialize");
    std::fprintf(stderr, "Is an NVIDIA driver installed? (nvapi64.dll must be present in System32)\n");
    return 1;
  }

  int rc;
  if (mode == "list") {
    rc = cmd_list();
  } else if (mode == "enable") {
    rc = cmd_enable(argc, argv);
  } else if (mode == "disable") {
    rc = cmd_disable(argc, argv);
  } else {
    std::fprintf(stderr, "Unknown mode '%s'.\n", mode.c_str());
    print_usage();
    rc = 2;
  }

  NvAPI_Unload();
  return rc;
}
