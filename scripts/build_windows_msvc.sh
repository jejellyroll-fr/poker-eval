#!/usr/bin/env bash
#
# Configure, build, and test the project with the CMake "windows-msvc" preset.
# Run this script from Git Bash or similar on a Windows machine with Visual Studio installed.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
PRESET="windows-msvc"
BUILD_DIR="${ROOT_DIR}/build/${PRESET}"
CMAKE_CONFIGURATION="${CMAKE_CONFIGURATION:-Release}"
GPU_MODE="${GPU_MODE:-auto}"   # cpu|cuda|opencl|both|auto

case "${GPU_MODE}" in
  cpu)
    GPU_ARGS=(-DBUILD_GPU=OFF -DENABLE_CUDA=OFF -DENABLE_OPENCL=OFF)
    ;;
  cuda)
    GPU_ARGS=(-DBUILD_GPU=ON -DENABLE_CUDA=ON -DENABLE_OPENCL=OFF)
    ;;
  opencl)
    GPU_ARGS=(-DBUILD_GPU=ON -DENABLE_CUDA=OFF -DENABLE_OPENCL=ON)
    ;;
  both|all)
    GPU_ARGS=(-DBUILD_GPU=ON -DENABLE_CUDA=ON -DENABLE_OPENCL=ON)
    ;;
  auto)
    GPU_ARGS=()
    ;;
  *)
    echo "Unknown GPU_MODE='${GPU_MODE}'. Expected cpu|cuda|opencl|both|auto." >&2
    exit 1
    ;;
esac

EXTRA_CMAKE_ARGS=("$@")

echo "==> Configuring (${PRESET}, config=${CMAKE_CONFIGURATION}, GPU_MODE=${GPU_MODE})"
cmake --preset "${PRESET}" -DCMAKE_BUILD_TYPE="${CMAKE_CONFIGURATION}" \
      "${GPU_ARGS[@]}" "${EXTRA_CMAKE_ARGS[@]}"

echo "==> Building (${CMAKE_CONFIGURATION})"
cmake --build --preset "${PRESET}" --config "${CMAKE_CONFIGURATION}" --parallel

echo "==> Running tests"
ctest --test-dir "${BUILD_DIR}" -C "${CMAKE_CONFIGURATION}" --output-on-failure

echo "Build completed successfully for preset ${PRESET} (${CMAKE_CONFIGURATION})"
