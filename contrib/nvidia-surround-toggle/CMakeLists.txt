# Standalone build for the NVIDIA Surround toggle helper CLI.
# This project is intentionally independent of the Sunshine build; it only reuses the NVAPI
# headers vendored in the Sunshine repo. Build it from an MSYS2 UCRT64 shell:
#
#   cd contrib/nvidia-surround-toggle
#   cmake -B build -G Ninja -S .
#   ninja -C build
#
# Produces build/nvidia-surround-toggle.exe.
cmake_minimum_required(VERSION 3.20)
project(nvidia-surround-toggle CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(FATAL_ERROR "nvidia-surround-toggle is Windows + NVIDIA only.")
endif()

# Reuse the NVAPI headers vendored in the Sunshine repo by default; override with -DNVAPI_DIR=...
set(NVAPI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../third-party/nvapi"
        CACHE PATH "Directory containing nvapi.h and nvapi_interface.h")
if(NOT EXISTS "${NVAPI_DIR}/nvapi.h")
    message(FATAL_ERROR
            "nvapi.h not found in '${NVAPI_DIR}'. "
            "Run `git submodule update --init third-party/nvapi` in the Sunshine repo, "
            "or pass -DNVAPI_DIR=<path to NVAPI headers>.")
endif()

add_executable(nvidia-surround-toggle
        main.cpp
        nvapi_loader.cpp)

target_include_directories(nvidia-surround-toggle SYSTEM PRIVATE "${NVAPI_DIR}")

# NVAPI is loaded dynamically from nvapi64.dll at runtime (see nvapi_loader.cpp), so no NVAPI
# import library is linked. Only standard Win32 libraries (already default) are required.
