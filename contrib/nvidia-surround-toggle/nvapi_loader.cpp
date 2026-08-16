/**
 * @file nvapi_loader.cpp
 * @brief Minimal dynamic NVAPI loader for the surround-toggle CLI.
 *
 * NVAPI ships as an MSVC static import library (nvapi64.lib) which does not link cleanly under
 * MinGW/UCRT64. To stay toolchain-agnostic we instead load nvapi64.dll at runtime, resolve
 * `nvapi_QueryInterface`, and dispatch each NvAPI_* call through the interface-id table. This
 * mirrors the approach Sunshine itself uses in
 * `src/platform/windows/nvprefs/nvapi_opensource_wrapper.cpp`; only the subset of functions this
 * tool needs is wrapped here.
 */
// standard includes
#include <map>

// nvapi must be included before <windows.h>: windows.h pulls in <sal.h>, which would pre-define
// __success and suppress nvapi's own SAL macro handling. Under GCC, __NVAPI_EMPTY_SAL also keeps
// nvapi's SAL macros defined through the whole header (nvapi_lite_salend.h would otherwise strip
// them mid-header, breaking NVAPI_INTERFACE); we include salend.h explicitly afterward to clean
// up. See main.cpp and src/platform/windows/nvprefs/driver_settings.h for the full rationale.
#if defined(__GNUC__)
  #define __NVAPI_EMPTY_SAL
#endif
#include <nvapi.h>
#if defined(__GNUC__)
  #undef __NVAPI_EMPTY_SAL
  #include <nvapi_lite_salend.h>
#endif

// platform includes
#include <windows.h>

// This must be the last include: it defines the interface-id table used to resolve functions.
#include <nvapi_interface.h>

namespace {
  std::map<const char *, void *> interfaces;  ///< Resolved NvAPI_* function pointers keyed by name.
  HMODULE dll = nullptr;  ///< Handle to the loaded nvapi64.dll.

  /**
   * @brief Dispatch a call to a dynamically-resolved NVAPI function.
   *
   * @tparam Func The declared NvAPI_* function type (used via decltype at each call site).
   * @param name The NvAPI_* symbol name to look up in the resolved interface table.
   * @param args Arguments forwarded to the resolved function.
   * @return The NVAPI status, or NVAPI_API_NOT_INITIALIZED / NVAPI_NOT_SUPPORTED if unresolved.
   */
  template<typename Func, typename... Args>
  NvAPI_Status call_interface(const char *name, Args... args) {
    auto func = (Func *) interfaces[name];
    if (!func) {
      return interfaces.empty() ? NVAPI_API_NOT_INITIALIZED : NVAPI_NOT_SUPPORTED;
    }
    return func(args...);
  }
}  // namespace

#undef NVAPI_INTERFACE
/**
 * @def NVAPI_INTERFACE
 * @brief Calling convention used for the NvAPI_* definitions provided by this translation unit.
 */
#define NVAPI_INTERFACE NvAPI_Status __cdecl

/**
 * @brief NVAPI export used to resolve function pointers by interface ID.
 *
 * @param id NVAPI interface ID from the generated interface table.
 * @return Function pointer for the requested NVAPI interface, or nullptr.
 */
extern void *__cdecl nvapi_QueryInterface(NvU32 id);

NVAPI_INTERFACE

/**
 * @brief Load nvapi64.dll and resolve the subset of interfaces this tool uses.
 *
 * @return NVAPI_OK on success, or NVAPI_LIBRARY_NOT_FOUND when the DLL cannot be loaded.
 */
NvAPI_Initialize() {
  if (dll) {
    return NVAPI_OK;
  }

#ifdef _WIN64
  auto dll_name = "nvapi64.dll";
#else
  auto dll_name = "nvapi.dll";
#endif

  if ((dll = LoadLibraryEx(dll_name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32))) {
    if (auto query_interface = (decltype(nvapi_QueryInterface) *) GetProcAddress(dll, "nvapi_QueryInterface")) {
      for (const auto &item : nvapi_interface_table) {
        interfaces[item.func] = query_interface(item.id);
      }
      return NVAPI_OK;
    }
  }

  NvAPI_Unload();
  return NVAPI_LIBRARY_NOT_FOUND;
}

/**
 * @brief Unload NVAPI and clear all resolved interface pointers.
 *
 * @return NVAPI_OK after cleanup.
 */
NVAPI_INTERFACE NvAPI_Unload() {
  if (dll) {
    interfaces.clear();
    FreeLibrary(dll);
    dll = nullptr;
  }
  return NVAPI_OK;
}

/**
 * @brief Forward NvAPI_GetErrorMessage to the loaded NVAPI DLL.
 */
NVAPI_INTERFACE NvAPI_GetErrorMessage(NvAPI_Status nr, NvAPI_ShortString szDesc) {
  return call_interface<decltype(NvAPI_GetErrorMessage)>("NvAPI_GetErrorMessage", nr, szDesc);
}

/**
 * @brief Forward NvAPI_EnumPhysicalGPUs to the loaded NVAPI DLL.
 */
NVAPI_INTERFACE NvAPI_EnumPhysicalGPUs(NvPhysicalGpuHandle nvGPUHandle[NVAPI_MAX_PHYSICAL_GPUS], NvU32 *pGpuCount) {
  return call_interface<decltype(NvAPI_EnumPhysicalGPUs)>("NvAPI_EnumPhysicalGPUs", nvGPUHandle, pGpuCount);
}

/**
 * @brief Forward NvAPI_GPU_GetConnectedDisplayIds to the loaded NVAPI DLL.
 */
NVAPI_INTERFACE NvAPI_GPU_GetConnectedDisplayIds(NvPhysicalGpuHandle hPhysicalGpu, NV_GPU_DISPLAYIDS *pDisplayIds, NvU32 *pDisplayIdCount, NvU32 flags) {
  return call_interface<decltype(NvAPI_GPU_GetConnectedDisplayIds)>("NvAPI_GPU_GetConnectedDisplayIds", hPhysicalGpu, pDisplayIds, pDisplayIdCount, flags);
}

/**
 * @brief Forward NvAPI_Mosaic_EnumDisplayGrids to the loaded NVAPI DLL.
 */
NVAPI_INTERFACE NvAPI_Mosaic_EnumDisplayGrids(NV_MOSAIC_GRID_TOPO *pGridTopologies, NvU32 *pGridCount) {
  return call_interface<decltype(NvAPI_Mosaic_EnumDisplayGrids)>("NvAPI_Mosaic_EnumDisplayGrids", pGridTopologies, pGridCount);
}

/**
 * @brief Forward NvAPI_Mosaic_ValidateDisplayGrids to the loaded NVAPI DLL.
 */
NVAPI_INTERFACE NvAPI_Mosaic_ValidateDisplayGrids(NvU32 setTopoFlags, NV_MOSAIC_GRID_TOPO *pGridTopologies, NV_MOSAIC_DISPLAY_TOPO_STATUS *pTopoStatus, NvU32 gridCount) {
  return call_interface<decltype(NvAPI_Mosaic_ValidateDisplayGrids)>("NvAPI_Mosaic_ValidateDisplayGrids", setTopoFlags, pGridTopologies, pTopoStatus, gridCount);
}

/**
 * @brief Forward NvAPI_Mosaic_SetDisplayGrids to the loaded NVAPI DLL.
 */
NVAPI_INTERFACE NvAPI_Mosaic_SetDisplayGrids(NV_MOSAIC_GRID_TOPO *pGridTopologies, NvU32 gridCount, NvU32 setTopoFlags) {
  return call_interface<decltype(NvAPI_Mosaic_SetDisplayGrids)>("NvAPI_Mosaic_SetDisplayGrids", pGridTopologies, gridCount, setTopoFlags);
}
